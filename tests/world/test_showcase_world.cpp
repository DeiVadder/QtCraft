#include "common/content/loader/QcBaseContent.h"
#include "common/content/loader/QcContentPackLoader.h"
#include "common/core/QcAabb.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcInProcessTransport.h"
#include "common/world/QcDayCycle.h"
#include "QcRawClient.h"
#include "QcStoredWorld.h"
#include "server/core/QcServer.h"
#include "server/storage/QcWorldLibrary.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillagerTrades.h"
#include "server/villages/QcVillages.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worldgen/showcase/QcShowcaseGenerator.h"
#include "worldgen/villages/QcVillagePlan.h"

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
    // A pen of the showcase (a structure) with the kinds of creatures it holds, how many, and the
    // box it takes in the world.
    struct ShowcasePen
    {
        QString structure;
        QStringList kinds;
        qsizetype creatures = 0;
        QcAabb box;
    };

    // Finds every pen's placement in the showcase plan, notes its box and loads the chunks around it.
    void placePens(QcServer &server, const QcShowcaseGenerator &generator, QList<ShowcasePen> &pens) const
    {
        for (ShowcasePen &pen : pens) {
            const auto placement = std::ranges::find_if(generator.plan().placements, [&](const QcStructurePlacement &placed) {
                return placed.structure().name() == pen.structure;
            });
            QVERIFY2(placement != generator.plan().placements.end(), qPrintable(pen.structure));
            const QcStructure &structure = placement->structure();
            const QcBlockPos a = placement->toWorld(QcBlockPos{0, 0, 0});
            const QcBlockPos b = placement->toWorld(QcBlockPos{structure.sizeX() - 1, structure.sizeY() - 1, structure.sizeZ() - 1});
            pen.box = {{double(std::min(a.x, b.x)), double(a.y), double(std::min(a.z, b.z))},
                       {double(std::max(a.x, b.x) + 1), double(b.y + 1), double(std::max(a.z, b.z) + 1)}};
            for (int x = int(pen.box.min.x) - 16; x <= int(pen.box.max.x) + 16; x += 16) {
                for (int z = int(pen.box.min.z) - 16; z <= int(pen.box.max.z) + 16; z += 16) {
                    server.world().loadChunk(QcBlockPos{x, 0, z}.chunk());
                }
            }
        }
    }

    // Names a pen whose count is off: its box, every creature of its kinds with health and position,
    // and every departure of such a creature so far (with its reason and last position).
    QString penReport(QcServer &server, const ShowcasePen &pen, const QStringList &departures) const
    {
        QString report = u"%1 box (%2 %3 %4)-(%5 %6 %7):"_s.arg(pen.structure)
                             .arg(pen.box.min.x).arg(pen.box.min.y).arg(pen.box.min.z)
                             .arg(pen.box.max.x).arg(pen.box.max.y).arg(pen.box.max.z);
        for (const QcCreature &mob : server.mobs().mobs()) {
            const QString &kind = m_content->mobs()[mob.type].name;
            if (pen.kinds.contains(kind)) {
                const QcVec3 &at = mob.motion.position;
                report += u" [%1 #%2 health %3 at %4 %5 %6]"_s.arg(kind).arg(mob.id).arg(mob.health).arg(at.x).arg(at.y).arg(at.z);
            }
        }
        for (const QString &departure : departures) {
            if (std::ranges::any_of(pen.kinds, [&](const QString &kind) { return departure.startsWith(kind); })) {
                report += u" departed: "_s + departure;
            }
        }
        return report;
    }

    // Water creatures whose middle is not in a liquid.
    qsizetype fishOnLand(QcServer &server) const
    {
        return std::ranges::count_if(server.mobs().mobs(), [&](const QcCreature &mob) {
            const QcMobDefinition &kind = m_content->mobs()[mob.type];
            const QcVec3 centre = mob.motion.position + QcVec3{0.0, kind.height * 0.5, 0.0};
            return kind.aquatic && !QcPhysics::isLiquidAt(server.world(), QcPhysics::blockContaining(centre));
        });
    }

    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

    std::unique_ptr<QcServer> makeServer(const QString &directory, const QString &generator = u"showcase"_s, qint64 seed = 0)
    {
        QcServerConfig config;
        config.worldDirectory = directory;
        config.generator = generator;
        config.seed = seed;
        config.viewDistance = 1;
        config.autosaveSeconds = 0;
        // The setup is looked at as a whole, without a player near it.
        config.keepChunksLoaded = true;
        return std::make_unique<QcServer>(config, m_content, QcWorldGenerators::create(generator, m_content, seed));
    }

    // Creatures and filled containers' distinct items in the files of a saved world (chunks are read
    // only when needed, stage 31.3).
    std::pair<qsizetype, qsizetype> stored(const QString &directory) const
    {
        const QcResult<QcStoredWorld> world = QcStoredWorld::read(directory, *m_content);
        if (!world) {
            return {-1, -1};
        }
        qsizetype creatures = 0;
        for (const QcEntityRecord &record : world->entities) {
            creatures += record.creatures.size();
        }
        QSet<QcItemId> items;
        for (const QcBlockEntityRecord &record : world->blockEntities) {
            for (const QcBlockEntities::Container &container : record.containers) {
                for (const QcItemStack &stack : container.stacks) {
                    items.insert(stack.item);
                }
            }
        }
        return {creatures, items.size()};
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
        qsizetype residents = 0;
        {
            std::unique_ptr<QcServer> server = makeServer(directory.path());
            QVERIFY(server->openWorld());

            // Noon that stands still, no creatures of their own accord, all creatures in their pens.
            QCOMPARE(server->worldTime(), 150.0);
            server->tick();
            server->tick();
            QCOMPARE(server->worldTime(), 150.0);
            // A clear sky that stays (decision E-395).
            QCOMPARE(server->systems().weather.state().kind, QcWeatherKind::Clear);
            QCOMPARE(server->systems().weather.state().rain, 0.0f);
            // Besides them, residents of the village rows in the chunks loaded so far (decision E-411).
            residents = std::ranges::count_if(server->mobs().mobs(), [](const QcCreature &mob) { return mob.isVillager() && mob.villager->village != 0; });
            // The cats of the village rows' houses (decision E-480) live there too.
            const int cat = *m_content->mobIndex(u"qtcraft:cat"_s);
            const auto setupCats = std::ranges::count(QcShowcaseGenerator(m_content).setup().mobs, u"qtcraft:cat"_s, &QcWorldSetup::Mob::mob);
            residents += std::ranges::count(server->mobs().mobs(), cat, &QcCreature::type) - setupCats;
            QCOMPARE(server->mobs().mobs().size() - residents, mobs);
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
        QCOMPARE(info.value(u"gameRules"_s).toObject().value(u"weatherCycle"_s).toBool(true), false);
        QCOMPARE(info.value(u"weather"_s).toObject().value(u"kind"_s).toString(), u"clear"_s);

        // Opened again, nothing is set up twice.
        std::unique_ptr<QcServer> again = makeServer(directory.path());
        QVERIFY(again->openWorld());
        QCOMPARE(again->mobs().mobs().size(), 0);
        QCOMPARE(again->worldTime(), 150.0);
        QVERIFY(again->stop());
        QCOMPARE(stored(directory.path()).first, mobs + residents);
    }

    void setupCountsAsDoneOnlyOnceSaved()
    {
        QTemporaryDir directory;
        const QDir world(directory.path());
        const int mobs = expectedMobs();

        // The block entities (chests) cannot be written, a file stands where their folder belongs:
        // the first save breaks off, and nothing of it is written (decision E-373).
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
        QVERIFY(QDir(world.filePath(u"entities"_s)).entryList({u"r.*"_s}, QDir::Files).isEmpty());
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
        QCOMPARE(third->mobs().mobs().size(), 0);
        QVERIFY(third->stop());
        const auto [creatures, items] = stored(directory.path());
        QCOMPARE(creatures, mobs);
        QVERIFY(items >= m_content->itemCount());
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
        // world.json of format 1 (before the showcase), 2 (before stage 31.1), 3 (before stage
        // 31.2), 9 (before the villages of stage 37.2), 10 (before the villagers of stage 37.2b), 11
        // (before the trading of stage 37.2c), 12 (before the entity components of package 37.R2), 13
        // (before the projectiles of package 52.1a), 14 (before the block entity tables of package
        // 37.R2b) and 15 (before the village ids of package 52.7): a world of an older development
        // version is refused, not converted (decision E-356), and nothing is written.
        for (const int version : {1, 2, 3, 9, 10, 11, 12, 13, 14, 15}) {
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
        // Fourteen enclosures are wider than five chunks (silverfish, decision E-453).
        config.viewDistance = 4;
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

    // Each row of the village area is a village of its own, and residents move into all its beds
    // although creatures do not spawn in the showcase (decision E-411).
    void villageRowsAreLivedIn()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QList<std::shared_ptr<const QcVillagePlan>> plans = QcShowcaseGenerator(m_content).plan().villages;
        QCOMPARE(plans.size(), m_content->villageStyles().size());
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QcChunkPos low = QcBlockPos{plan->minX, 0, plan->minZ}.chunk();
            const QcChunkPos high = QcBlockPos{plan->maxX, 0, plan->maxZ}.chunk();
            for (int x = low.x; x <= high.x; ++x) {
                for (int z = low.z; z <= high.z; ++z) {
                    server->world().loadChunk({x, z});
                }
            }
        }
        server->tick();
        server->tick();

        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QcVillage *village = server->world().villages().find(plan->id);
            QVERIFY2(village, qPrintable(plan->style));
            QCOMPARE(village->style, plan->style);
            QVERIFY2(!village->beds.isEmpty(), qPrintable(plan->style));
            QCOMPARE(village->population, village->beds.size());
            // Only the row's own: no podium of another area, no spot of another row.
            const auto inRow = [&plan](const QcVillage::Spot &spot) { return plan->covers(spot.at.x, spot.at.z, 0); };
            QVERIFY(std::ranges::all_of(village->beds, inRow));
            QVERIFY(std::ranges::all_of(village->workstations, inRow));
            QVERIFY(std::ranges::all_of(village->beds, [](const QcVillage::Spot &bed) { return bed.claimedBy != 0; }));
            const auto residents = std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                return mob.isVillager() && mob.villager->village == plan->id && mob.villager->style == plan->style;
            });
            QCOMPARE(residents, village->population);
            // The villager of its last bed is a child (decision E-415).
            const auto children = std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                return mob.isVillager() && mob.villager->village == plan->id && mob.isYoung() && mob.villager->home == village->beds.constLast().at;
            });
            QCOMPARE(children, 1);
            // Its fields are the crop cells of its pieces (what its farmers harvest).
            QVERIFY2(!plan->fields.isEmpty(), qPrintable(plan->style));
            for (const QcBlockPos &field : plan->fields) {
                QVERIFY2(m_content->block(*server->world().blockAt(field)).crop.isCrop(), qPrintable(plan->style));
            }
        }
    }

    // The village station (decision E-490): the bed beyond the first row makes that row's village
    // grow, the beds and composter away from the rows found a settlement, and the two homeless
    // villagers beside it move in and claim beds; noting the generated points again (a chunk that
    // enters again) changes nothing.
    void villageStationGrowsAVillageAndFoundsASettlement()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        QList<QcBlockPos> points;
        for (const QList<QcBlockPos> &chunk : generator.plan().pointsOfInterestByChunk) {
            points.append(chunk);
        }
        // The growth bed lies west of the settlement's three beds and composter.
        QCOMPARE(points.size(), 5);
        std::ranges::sort(points, {}, [](QcBlockPos pos) { return pos.x; });
        const QcBlockPos growth = points.first();
        const std::shared_ptr<const QcVillagePlan> firstRow = generator.plan().villages.first();
        QVERIFY(growth.x > firstRow->maxX + QcVillage::reach);
        QVERIFY(growth.x - firstRow->maxX <= QcVillages::joinDistance);
        for (const QcBlockPos &point : std::as_const(points)) {
            for (int dx = -3; dx <= 3; ++dx) {
                for (int dz = -3; dz <= 3; ++dz) {
                    server->world().loadChunk({point.chunk().x + dx, point.chunk().z + dz});
                }
            }
        }
        for (int tick = 0; tick < 2 * QcVillagePopulation::villageLifeTicks + 2; ++tick) {
            server->tick();
        }

        const QcVillages &villages = server->world().villages();
        const QcVillage *row = villages.find(firstRow->id);
        QVERIFY(row);
        QCOMPARE(row->maxX, growth.x);
        QCOMPARE(villages.at(growth), row);
        QVERIFY(std::ranges::any_of(row->beds, [growth](const QcVillage::Spot &bed) { return bed.at == growth; }));

        const QcVillage *settlement = villages.at(points.at(1));
        QVERIFY(settlement);
        QVERIFY(settlement->settlement());
        QCOMPARE(settlement->id, QcVillage::settlementId(points.at(1)));
        QCOMPARE(settlement->beds.size(), 3);
        QCOMPARE(settlement->workstations.size(), 1);
        // The two settlers move in; with mobGriefing on they may pick up the bread and have a child
        // in the third bed within these ticks, depending on the order in which the chunks arrive.
        const auto settlers = std::ranges::count_if(server->mobs().mobs(), [settlement](const QcCreature &mob) {
            return mob.isVillager() && mob.villager->village == settlement->id && mob.villager->home.has_value();
        });
        QVERIFY(settlers >= 2 && settlers <= 3);
        QCOMPARE(settlement->population, int(settlers));
        QCOMPARE(settlement->freeBeds(), 3 - int(settlers));

        const QList<QcVillage> before = villages.villages();
        for (const QcBlockPos &point : std::as_const(points)) {
            server->world().villages().notePointOfInterest(point, *server->world().blockAt(point));
        }
        QCOMPARE(server->world().villages().villages(), before);
    }

    // The rows serve as the trading station (decision E-414): their villagers take the professions
    // of the rows' workstations, and farmers, smiths and fishers there all have offers.
    void villageRowsTrade()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QList<std::shared_ptr<const QcVillagePlan>> plans = QcShowcaseGenerator(m_content).plan().villages;
        for (const std::shared_ptr<const QcVillagePlan> &plan : plans) {
            const QcChunkPos low = QcBlockPos{plan->minX, 0, plan->minZ}.chunk();
            const QcChunkPos high = QcBlockPos{plan->maxX, 0, plan->maxZ}.chunk();
            for (int x = low.x; x <= high.x; ++x) {
                for (int z = low.z; z <= high.z; ++z) {
                    server->world().loadChunk({x, z});
                }
            }
        }
        for (int tick = 0; tick < 45; ++tick) {
            server->tick();
        }

        const QcVillagerTrades &trades = server->mobs().villagers().trades(*m_content->mobIndex(u"qtcraft:villager"_s));
        QSet<QString> trading;
        for (const QcCreature &mob : server->mobs().mobs()) {
            if (mob.isVillager() && mob.villager->village != 0 && !trades.offersOf(*mob.villager).isEmpty()) {
                trading.insert(mob.villager->profession);
            }
        }
        QCOMPARE(trading, (QSet<QString>{u"qtcraft:farmer"_s, u"qtcraft:smith"_s, u"qtcraft:fisher"_s}));
    }

    void thePetCornerHasItsPets()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        server->tick();

        // The dog of a made-up owner sits; wolves and the ocelot stay in their pens (decision E-480).
        const int dog = *m_content->mobIndex(u"qtcraft:dog"_s);
        const auto pet = std::ranges::find_if(server->mobs().mobs(), [dog](const QcCreature &mob) { return mob.type == dog && mob.isTame(); });
        QVERIFY(pet != server->mobs().mobs().end());
        QCOMPARE(pet->tame->owner, u"showcase"_s);
        QVERIFY(pet->sits());
        QCOMPARE(pet->pose, QcPose::Sitting);
        const QcVec3 sat = pet->motion.position;
        for (int i = 0; i < 40; ++i) {
            server->tick();
        }
        const auto still = std::ranges::find_if(server->mobs().mobs(), [dog](const QcCreature &mob) { return mob.type == dog && mob.isTame(); });
        QVERIFY((still->motion.position - sat).length() < 0.1);
        for (const char *name : {"wolf", "ocelot", "cat"}) {
            const int type = *m_content->mobIndex(u"qtcraft:"_s + QString::fromLatin1(name));
            QVERIFY2(std::ranges::count_if(server->mobs().mobs(), [type](const QcCreature &mob) { return mob.type == type && mob.confined; }) >= 1, name);
        }
        QVERIFY(server->stop());
    }

    // The golem farm station (decision E-470): its three villagers come rested from the setup, panic
    // at the zombie behind glass and summon an iron golem onto the floor of the pen within a few
    // checks, although creatures do not spawn by themselves in the showcase. The workshop beside it shows the T of iron blocks.
    void theGolemFarmSummonsAGolem()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        const QcShowcasePlan &plan = generator.plan();
        const auto farm = std::ranges::find_if(plan.placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name().endsWith(u"golem_farm"_s);
        });
        QVERIFY(farm != plan.placements.end());
        QVERIFY(std::ranges::any_of(plan.placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name().endsWith(u"golem_workshop"_s);
        }));
        const QcBlockPos low = farm->origin();
        const QcBlockPos high = low.offset(farm->sizeX() - 1, 0, farm->sizeZ() - 1);
        const int reach = 32;
        for (int x = (low.x - reach) >> 4; x <= (high.x + reach) >> 4; ++x) {
            for (int z = (low.z - reach) >> 4; z <= (high.z + reach) >> 4; ++z) {
                server->world().loadChunk({x, z});
            }
        }

        const int golemType = *m_content->mobIndex(u"qtcraft:iron_golem"_s);
        const auto golemsInFarm = [&] {
            return std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                const QcBlockPos at = QcPhysics::blockContaining(mob.motion.position);
                return mob.type == golemType && at.x >= low.x - 8 && at.x <= high.x + 8 && at.z >= low.z - 8 && at.z <= high.z + 8;
            });
        };
        // Villagers look out every second, villages live every five seconds: four checks at most.
        int ticks = 0;
        for (; ticks < 4 * 100 && golemsInFarm() == 0; ++ticks) {
            server->tick();
        }
        // None at the start: the villagers summoned it.
        QVERIFY(ticks > 0);
        QVERIFY2(golemsInFarm() >= 1, qPrintable(QString::number(ticks)));

        // It stands on the stone floor of the pen, not on the glass or the roof of the zombie's cell
        // (golems of other stations aside).
        for (const QcCreature &mob : server->mobs().mobs()) {
            const QcBlockPos at = QcPhysics::blockContaining(mob.motion.position);
            if (mob.type != golemType || at.x < low.x - 8 || at.x > high.x + 8 || at.z < low.z - 8 || at.z > high.z + 8) {
                continue;
            }
            QVERIFY2(at.y == low.y + 1, qPrintable(QString::number(at.y - low.y)));
            QVERIFY2(at.x > low.x && at.x < high.x && at.z > low.z && at.z < high.z,
                     qPrintable(u"%1 %2 in %3..%4 %5..%6"_s.arg(at.x).arg(at.z).arg(low.x).arg(high.x).arg(low.z).arg(high.z)));
        }
    }

    // The dungeon station (decision E-520): its spawner brings forth confined zombies, its chest
    // holds loot from the dungeon table, and the torch of its corridor stub lies outside the light
    // reach of the spawn area.
    void theDungeonStationHasItsSpawnerAndLoot()
    {
        QTemporaryDir directory;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        const auto station = std::ranges::find_if(generator.plan().placements, [](const QcStructurePlacement &placement) {
            return placement.structure().name().endsWith(u"dungeon_station"_s);
        });
        QVERIFY(station != generator.plan().placements.end());
        const QcStructure &structure = station->structure();
        QCOMPARE(structure.spawners.size(), 1);
        QCOMPARE(structure.loot.size(), 1);
        const QcBlockPos spawner = station->toWorld(structure.spawners[0].at);
        const QcBlockPos chest = station->toWorld(structure.loot[0].at);
        server->world().loadChunk(spawner.chunk());
        server->world().loadChunk(chest.chunk());

        const QcSpawners::Entry *entry = server->world().spawners().at(spawner);
        QVERIFY(entry);
        QCOMPARE(entry->mob, u"qtcraft:zombie"_s);
        QVERIFY(entry->confined);
        const QcBlockEntities::Container *container = server->blockEntities().container(chest);
        QVERIFY(container && std::ranges::any_of(container->stacks, [](QcItemStack stack) { return !stack.isEmpty(); }));

        const QcBlockId torch = *m_content->stateId(u"qtcraft:torch[attach=north]"_s);
        int torches = 0;
        for (int x = 0; x < station->sizeX(); ++x) {
            for (int z = 0; z < station->sizeZ(); ++z) {
                for (int y = 0; y < structure.sizeY(); ++y) {
                    const QcBlockPos at = station->toWorld(QcBlockPos{x, y, z});
                    if (server->world().loadChunk(at.chunk()); server->world().blockAt(at) == torch) {
                        ++torches;
                        const int distance = std::max(0, std::abs(at.x - spawner.x) - 4) + std::max(0, std::abs(at.y - spawner.y) - 1)
                                             + std::max(0, std::abs(at.z - spawner.z) - 4);
                        QVERIFY2(distance >= 15, qPrintable(QString::number(distance)));
                    }
                }
            }
        }
        QCOMPARE(torches, 1);
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

    // The pens of stages 52.2 and 52.4 (decisions E-451 to E-453, E-500 to E-503): the spider cave,
    // the silverfish wall, the chicken coop, the cow pasture, the aquarium and the fishing pier hold
    // their creatures for a minute of game time; none escapes or hides (fish stay in their water).
    void creaturePensOfStage52HoldTheirCreatures()
    {
        QTemporaryDir directory;
        // Outlives the server, whose departure listener writes to it.
        QStringList departures;
        std::unique_ptr<QcServer> server = makeServer(directory.path());
        QVERIFY(server->openWorld());
        const QcShowcaseGenerator generator(m_content);
        using Pen = ShowcasePen;
        QList<Pen> pens{{u"qtcraft:spider_cave"_s, {u"qtcraft:spider"_s, u"qtcraft:cave_spider"_s}, 3, {}},
                        {u"qtcraft:silverfish_wall"_s, {u"qtcraft:silverfish"_s}, 3, {}},
                        {u"qtcraft:chicken_coop"_s, {u"qtcraft:chicken"_s}, 4, {}},
                        {u"qtcraft:cow_pasture"_s, {u"qtcraft:cow"_s}, 3, {}},
                        {u"qtcraft:aquarium"_s, {u"qtcraft:cod"_s, u"qtcraft:salmon"_s}, 7, {}},
                        {u"qtcraft:fishing_pier"_s, {u"qtcraft:cod"_s}, 3, {}}};
        placePens(*server, generator, pens);
        if (QTest::currentTestFailed()) {
            return;
        }
        server->mobs().onDeparture([&](const QcCreature &mob, QcMobManager::Departure why) {
            const QcVec3 &at = mob.motion.position;
            departures += u"%1 #%2 reason %3 health %4 at %5 %6 %7"_s.arg(m_content->mobs()[mob.type].name).arg(mob.id).arg(int(why))
                              .arg(mob.health).arg(at.x).arg(at.y).arg(at.z);
        });

        const auto inside = [&](const Pen &pen) {
            return std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                const QcVec3 &at = mob.motion.position;
                return pen.kinds.contains(m_content->mobs()[mob.type].name) && at.x >= pen.box.min.x && at.x <= pen.box.max.x
                       && at.z >= pen.box.min.z && at.z <= pen.box.max.z && at.y >= pen.box.min.y && at.y <= pen.box.max.y;
            });
        };
        // The creature area's row of glass pens holds the same kinds; cod live in two of the pens.
        const QcShowcaseAreaInfo row = *std::ranges::find(generator.plan().areas, u"creatures"_s, &QcShowcaseAreaInfo::name);
        const auto escaped = [&](const Pen &pen) {
            return std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
                const QcVec3 &at = mob.motion.position;
                const bool inRow = at.x >= row.origin.x && at.x <= row.origin.x + row.sizeX && at.z >= row.origin.z && at.z <= row.origin.z + row.sizeZ;
                const bool inPen = std::ranges::any_of(pens, [&](const Pen &other) {
                    return at.x >= other.box.min.x && at.x <= other.box.max.x && at.z >= other.box.min.z && at.z <= other.box.max.z;
                });
                return pen.kinds.contains(m_content->mobs()[mob.type].name) && !inRow && !inPen;
            });
        };
        for (int tick = 0; tick < 2; ++tick) {
            server->tick();
        }
        for (const Pen &pen : std::as_const(pens)) {
            QVERIFY2(inside(pen) == pen.creatures, qPrintable(penReport(*server, pen, departures)));
        }
        QHash<quint32, QcVec3> start;
        for (const QcCreature &mob : server->mobs().mobs()) {
            start.insert(mob.id, mob.motion.position);
        }
        for (int tick = 0; tick < 1200; ++tick) {
            server->tick();
        }
        // They are awake and walk about their pens.
        const auto moved = std::ranges::count_if(server->mobs().mobs(), [&](const QcCreature &mob) {
            return start.contains(mob.id) && (mob.motion.position - start.value(mob.id)).length() > 0.5;
        });
        QVERIFY2(moved >= 5, qPrintable(QString::number(moved)));
        for (const Pen &pen : std::as_const(pens)) {
            QVERIFY2(inside(pen) == pen.creatures && escaped(pen) == 0, qPrintable(penReport(*server, pen, departures)));
        }
        // The fish swim: none lies on land.
        QCOMPARE(fishOnLand(*server), 0);
    }
};

QTEST_GUILESS_MAIN(TestShowcaseWorld)
#include "test_showcase_world.moc"

#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// Checks saving, reconnecting and restarting, dropped items and saplings, with raw protocol
// messages.
class TestServerItemsAndSaves : public QcServerTestFixture
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void playersAreSavedOnDisconnectAndChunksByAutosave()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 1;
        join(config);
        QVERIFY(m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:glass")));
        ticks(20);
        m_server->waitForStorage();
        QVERIFY(QFile::exists(regionFile(dir.path(), m_feet.offset(1, 0, 0))));
        // The autosave wrote the player too; remove it so only the save on disconnect can recreate it.
        QVERIFY(QFile::remove(dir.filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Raw"_s))));
        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        // Saved with the world in the background (decision E-378).
        m_server->waitForStorage();
        QVERIFY(QFile::exists(dir.filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Raw"_s))));
    }

    void deathSurvivesReconnecting()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);

        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);

        QVERIFY(m_client->dead);
        QCOMPARE(m_client->health, 0.0);

        m_client->connection.send(QcMsg::Respawn{});
        QcTestUtil::pump();
        ticks(1);
        QVERIFY(!m_client->dead);
        QCOMPARE(m_client->health, 20.0);
    }

    void foodSurvivesReconnecting()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/feed 9"_s);

        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(2);

        QCOMPARE(m_client->food, 9);
    }

    void deathDropsTheInventoryOnlyWithoutKeepInventory()
    {
        join();
        QVERIFY(m_server->keepInventory());
        chat(u"/give dirt 5"_s);
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(countOf(item("qtcraft:dirt")), 5);
        QVERIFY(m_server->items().items().isEmpty());
        send(QcMsg::Respawn{});

        chat(u"/gamerule keepInventory false"_s);
        QVERIFY(!m_server->keepInventory());
        QVERIFY(m_client->chatLines.last().contains(u"false"_s));
        chat(u"/give stone_pickaxe"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:stone_pickaxe"))});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:dirt")), QcServer::craftingGridSlot, 2});
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        QCOMPARE(countOf(item("qtcraft:stone_pickaxe")), 0);
        QVERIFY(std::ranges::all_of(m_client->craftingGrid, &QcItemStack::isEmpty));
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        QCOMPARE(droppedCount(item("qtcraft:stone_pickaxe")), 1);
        // Dead players pick nothing up.
        ticks(30);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);
        // Respawned at the same spot, the player gathers the items again; none get lost on the way.
        send(QcMsg::Respawn{});
        idle(40);
        QCOMPARE(countOf(item("qtcraft:dirt")) + droppedCount(item("qtcraft:dirt")), 5);
        QVERIFY(countOf(item("qtcraft:dirt")) > 0);
    }

    void droppedItemsAreThrownAndSentToClients()
    {
        join();
        chat(u"/give dirt 5"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:dirt"))});
        send(QcMsg::DropItem{false});
        QCOMPARE(countOf(item("qtcraft:dirt")), 4);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 1);
        QCOMPARE(m_server->items().items().first().pickupDelay > 0.0, true);

        send(QcMsg::DropItem{true});
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);

        // Nothing to drop: nothing happens.
        send(QcMsg::DropItem{true});
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 5);

        ticks(2);
        int seen = 0;
        for (const QcItemStack &stack : std::as_const(m_client->items)) {
            QCOMPARE(stack.item, item("qtcraft:dirt"));
            seen += stack.count;
        }
        QCOMPARE(seen, 5);

        // Thrown items land a few metres ahead, out of the player's reach.
        ticks(static_cast<int>(QcItemEntityManager::thrownPickupDelay * QcPhysics::tickRate) + 5);
        QCOMPARE(countOf(item("qtcraft:dirt")), 0);
        for (const QcItemEntity &thrown : m_server->items().items()) {
            QVERIFY((thrown.motion.position - m_client->motion.position).length() > 1.5);
        }
    }

    void itemsMergeAndClientsLearnTheNewCount()
    {
        join();
        const QcVec3 spot{m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 0.5};
        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:dirt"), 3, 0};
        drop.position = spot;
        m_server->items().spawn(drop, 9001);
        drop.position = spot + QcVec3{0.3, 0.0, 0.0};
        m_server->items().spawn(drop, 9002);
        ticks(1);
        QCOMPARE(m_client->items.size(), 2);
        ticks(QcItemEntityManager::mergeIntervalTicks + 1);
        QCOMPARE(m_client->items.size(), 1);
        QCOMPARE(m_client->items.begin()->count, 6);
    }

    void saplingsArePlantedOnSuitableGroundAndBreakWithIt()
    {
        join();
        chat(u"/give oak_sapling 3"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:oak_sapling"))});
        const QcBlockPos grass = m_feet.offset(2, -1, 0);
        send(QcMsg::UseItem{grass, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:oak_sapling")));
        QCOMPARE(m_server->blockEntities().saplingsInOrder().size(), 1);
        QCOMPARE(countOf(item("qtcraft:oak_sapling")), 2);
        // Not on stone, and not on another sapling.
        const QcBlockPos stone = m_feet.offset(-2, -1, 0);
        m_server->world().setBlock(stone, id("qtcraft:stone"));
        send(QcMsg::UseItem{stone, QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(stone.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        send(QcMsg::UseItem{grass.offset(0, 1, 0), QcBlockFace::Up});
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 2, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(countOf(item("qtcraft:oak_sapling")), 2);

        // Breaking the ground breaks the sapling, which drops itself.
        const double grassSeconds = QcMining::breakSeconds(m_content->block(id("qtcraft:grass_block")), nullptr);
        send(QcMsg::StartMining{grass});
        ticks(static_cast<int>(std::ceil(grassSeconds * QcPhysics::tickRate)));
        send(QcMsg::FinishMining{grass});
        QCOMPARE(m_server->world().blockAt(grass), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(m_server->world().blockAt(grass.offset(0, 1, 0)), std::optional<QcBlockId>(qcAirBlockId));
        QCOMPARE(droppedCount(item("qtcraft:oak_sapling")), 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 1);
        ticks(1);
        QVERIFY(m_server->blockEntities().saplingsInOrder().isEmpty());
    }

    // A confirmed break (decision E-853) comes after its drops: a client that sees the BlockAck knows
    // the dropped items already (the control interface's `mine` then `collect`).
    void theAcknowledgedBreakFollowsItsDrops()
    {
        join();
        const QcBlockPos dirt = m_feet.offset(2, -1, 0);
        m_server->world().setBlock(dirt, id("qtcraft:dirt"));
        const double seconds = QcMining::breakSeconds(m_content->block(id("qtcraft:dirt")), nullptr);
        send(QcMsg::StartMining{dirt});
        ticks(static_cast<int>(std::ceil(seconds * QcPhysics::tickRate)));
        const qsizetype itemsBefore = m_client->items.size();

        // Handled at once, acknowledged only with the next world sync.
        m_client->connection.send(QcMsg::FinishMining{dirt, 7});
        QcTestUtil::pump();
        QCOMPARE(m_server->world().blockAt(dirt), std::optional<QcBlockId>(qcAirBlockId));
        QVERIFY(m_client->blockAcks.isEmpty());
        ticks(1);
        QCOMPARE(m_client->blockAcks.size(), 1);
        QCOMPARE(m_client->blockAcks.first().sequence, 7u);
        QCOMPARE(m_client->blockAcks.first().itemsKnown, itemsBefore + 1);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 1);
    }

    void despawningSaplingsPlantThemselves()
    {
        join();
        // Twenty old sapling items on grass, one per block; with a chance of 3/4 each, some take root.
        int spawned = 0;
        for (int dx = -3; dx <= 3 && spawned < 20; ++dx) {
            for (int dz = -3; dz <= 3 && spawned < 20; ++dz) {
                if (std::abs(dx) + std::abs(dz) < 2) {
                    continue;
                }
                QcItemEntityManager::Drop drop;
                drop.stack = {item("qtcraft:oak_sapling"), 1, 0};
                drop.position = {m_feet.x + dx + 0.5, static_cast<double>(m_feet.y), m_feet.z + dz + 0.5};
                drop.age = QcItemEntityManager::despawnSeconds - 0.01;
                drop.pickupDelay = 10.0;
                m_server->items().spawn(drop, static_cast<quint32>(9100 + spawned++));
            }
        }

        ticks(1);
        QVERIFY(m_server->items().items().isEmpty());

        int planted = 0;
        for (int dx = -3; dx <= 3; ++dx) {
            for (int dz = -3; dz <= 3; ++dz) {
                planted += m_server->world().blockAt(m_feet.offset(dx, 0, dz)) == id("qtcraft:oak_sapling") ? 1 : 0;
            }
        }
        QVERIFY2(planted >= 8 && planted <= 20, qPrintable(QString::number(planted)));
        QCOMPARE(m_server->blockEntities().saplingsInOrder().size(), planted);

        // Other items just vanish.
        QcItemEntityManager::Drop dirt;
        dirt.stack = {item("qtcraft:dirt"), 1, 0};
        dirt.position = {m_feet.x + 0.5, static_cast<double>(m_feet.y) + 3.0, m_feet.z + 3.5};
        dirt.age = QcItemEntityManager::despawnSeconds - 0.01;
        m_server->items().spawn(dirt, 9200);
        ticks(1);
        QVERIFY(m_server->items().items().isEmpty());
    }

    void itemsSaplingsAndGameRulesSurviveARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        chat(u"/gamerule keepInventory false"_s);

        QcItemEntityManager::Drop drop;
        drop.stack = {item("qtcraft:stone_pickaxe"), 1, 7};
        drop.position = {m_feet.x + 2.5, static_cast<double>(m_feet.y), m_feet.z + 2.5};
        m_server->items().spawn(drop, 9300);

        const QcBlockPos sapling = m_feet.offset(-2, 0, -2);
        QVERIFY(m_server->world().setBlock(sapling, id("qtcraft:birch_sapling")));
        m_server->plants().saplingPlaced(m_server->blockEntities(), sapling, id("qtcraft:birch_sapling"));

        ticks(1);
        QVERIFY(m_server->stop().has_value());
        QVERIFY(!QDir(dir.filePath(u"entities"_s)).entryList({u"r.*.qen"_s}, QDir::Files).isEmpty());

        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(m_feet);

        QVERIFY(!m_server->keepInventory());
        QCOMPARE(m_server->items().items().size(), 1);
        QCOMPARE(m_server->items().items().first().stack, (QcItemStack{item("qtcraft:stone_pickaxe"), 1, 7}));
        QCOMPARE(m_server->blockEntities().saplingsInOrder().size(), 1);
        QCOMPARE(m_server->blockEntities().saplingsInOrder().first().pos, sapling);
    }

    void failedSavesAreRetried()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        const QString regions = dir.filePath(u"regions"_s);
        QVERIFY(QDir().mkpath(regions));
        QVERIFY(QFile::setPermissions(regions, QFile::ReadOwner | QFile::ExeOwner));
        QVERIFY(m_server->world().setBlock(m_feet.offset(1, 0, 0), id("qtcraft:glass")));
        QVERIFY(!m_server->save().has_value());
        QVERIFY(QFile::setPermissions(regions, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(m_server->save().has_value());
        QVERIFY(QFile::exists(regionFile(dir.path(), m_feet.offset(1, 0, 0))));
    }
};

QTEST_GUILESS_MAIN(TestServerItemsAndSaves)
#include "test_server_items_and_saves.moc"

#include "common/physics/QcPhysics.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace Qt::StringLiterals;

// Checks the server side of crafting grids, the recipe book and machines, with raw protocol messages.
class TestServerCrafting : public QcServerTestFixture
{
    Q_OBJECT

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void inventoryGridCraftsAndTakesOnce()
    {
        join();
        chat(u"/give oak_log 2"_s);
        QCOMPARE(m_client->craftingSize, 2);

        // One log into the grid's top left cell: four planks are on offer.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 1});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);
        QCOMPARE(m_client->craftingResult, (QcItemStack{item("qtcraft:oak_planks"), 4, 0}));
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 4);
        QVERIFY(m_client->craftingResult.isEmpty());
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 4);

        // The third column and row of the grid only exist at a crafting table.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 2, 1});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 6, 1});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);

        // Closing returns what is left in the grid.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot + 3, 0});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        send(QcMsg::CloseCrafting{});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 1);
    }

    void hostileCraftingRequestsConserveItems()
    {
        join();
        chat(u"/give oak_log 7"_s);
        const auto total = [this] {
            int planks = countOf(item("qtcraft:oak_planks"));
            int logs = countOf(item("qtcraft:oak_log"));
            for (const QcItemStack &cell : std::as_const(m_client->craftingGrid)) {
                logs += cell.item == item("qtcraft:oak_log") ? cell.count : 0;
            }
            // Four planks per log: the sum in logs never changes.
            return logs * 4 + planks;
        };
        const int before = total();
        const QList<int> slotNumbers{-1, 0, 8, 35, 36, 38, 42, 44, 45, 1000, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
        const QList<int> counts{-1, 0, 1, 3, 64, 1000, std::numeric_limits<int>::min()};
        qsizetype round = 0;
        for (const int from : slotNumbers) {
            for (const int to : slotNumbers) {
                m_client->connection.send(QcMsg::MoveInventoryItem{from, to, counts[round++ % counts.size()]});
            }
        }
        for (const int count : counts) {
            m_client->connection.send(QcMsg::CraftRecipe{u"qtcraft:oak_planks"_s, count});
            m_client->connection.send(QcMsg::TakeCraftingResult{});
        }
        QcTestUtil::pump();
        ticks(2);
        QCOMPARE(total(), before);
        // 3 x 3-only cells stay empty without a table.
        for (const int cell : {2, 5, 6, 7, 8}) {
            QVERIFY(m_client->craftingGrid.value(cell).isEmpty());
        }
        for (const QcItemStack &stack : std::as_const(m_client->stacks)) {
            QVERIFY(stack.isEmpty() || (stack.count >= 1 && stack.count <= 64));
        }

        // The dead neither craft nor eat: give them something to craft from and something to eat first.
        chat(u"/give oak_log 2"_s);
        chat(u"/give raw_porkchop 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:raw_porkchop"))});
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 1});
        QVERIFY(!m_client->craftingResult.isEmpty());
        chat(u"/kill"_s);
        QVERIFY(m_client->dead);
        const QList<QcItemStack> stacks = m_client->stacks;
        send(QcMsg::CraftRecipe{u"qtcraft:oak_planks"_s, 1});
        send(QcMsg::TakeCraftingResult{});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_client->stacks, stacks);
    }

    void gridItemsSurviveDisconnectAndServerStop()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        join(config);
        chat(u"/give oak_log 5"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        // Disconnect with the grid full, then rejoin: the logs are back in the inventory.
        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        const auto rejoin = [this] {
            auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
            m_server->addClient(std::move(serverEnd));
            m_client = std::make_unique<RawClient>(std::move(clientEnd));
            m_client->hello(u"Raw"_s);
            QcTestUtil::pump();
            ticks(2);
        };
        rejoin();
        QCOMPARE(countOf(item("qtcraft:oak_log")), 5);
        QVERIFY(std::ranges::all_of(m_client->craftingGrid, &QcItemStack::isEmpty));

        // The same through a server stop.
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        rejoin();
        QCOMPARE(countOf(item("qtcraft:oak_log")), 5);
    }

    void recipeBookIsAllOrNothingAndNeedsATableForBigRecipes()
    {
        join();
        chat(u"/give oak_planks 3"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:crafting_table"_s, 1});
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 3);
        QCOMPARE(countOf(item("qtcraft:crafting_table")), 0);
        chat(u"/give oak_planks 9"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:crafting_table"_s, 5});
        QCOMPARE(countOf(item("qtcraft:crafting_table")), 3);
        QCOMPARE(countOf(item("qtcraft:oak_planks")), 0);

        chat(u"/give oak_planks 3"_s);
        chat(u"/give stick 2"_s);
        send(QcMsg::CraftRecipe{u"qtcraft:wooden_pickaxe"_s, 1});
        QCOMPARE(countOf(item("qtcraft:wooden_pickaxe")), 0);
        QVERIFY(m_client->chatLines.constLast().contains(u"crafting table"_s));
        send(QcMsg::CraftRecipe{u"qtcraft:no_such_recipe"_s, 1});
        QVERIFY(m_client->chatLines.constLast().contains(u"Unknown recipe"_s));

        // Place a table next to the player and use it: the 3 x 3 grid opens and the pickaxe works.
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:crafting_table"))});
        const QcBlockPos table = m_feet.offset(2, 0, 0);
        send(QcMsg::UseItem{table.offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(*m_server->world().blockAt(table), id("qtcraft:crafting_table"));
        send(QcMsg::UseItem{table, QcBlockFace::Up});
        QCOMPARE(m_client->openedCrafting, 3);
        QCOMPARE(m_client->craftingSize, 3);
        send(QcMsg::CraftRecipe{u"qtcraft:wooden_pickaxe"_s, 1});
        QCOMPARE(countOf(item("qtcraft:wooden_pickaxe")), 1);
        // Breaking the table away closes the big grid on the next take.
        m_server->world().setBlock(table, qcAirBlockId);
        send(QcMsg::TakeCraftingResult{});
        QCOMPARE(m_client->craftingSize, 2);
    }

    void furnaceSmeltsAndChecksItsSlots()
    {
        join();
        const QcBlockPos pos = placeFurnace();
        // The player looks north, so the furnace's front faces south, back at the player.
        QCOMPARE(m_server->world().blockAt(pos),
                 std::optional<QcBlockId>(m_content->withProperty(id("qtcraft:furnace"), u"facing"_s, u"south"_s)));
        QVERIFY(m_server->blockEntities().machine(pos));
        // Machine slots mean nothing before a machine is open.
        chat(u"/give raw_iron 3"_s);
        move(slotOf(item("qtcraft:raw_iron")), input);
        QCOMPARE(countOf(item("qtcraft:raw_iron")), 3);

        send(QcMsg::UseItem{pos, QcBlockFace::West});
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(pos));
        QCOMPARE(m_client->machineCategory, u"smelting"_s);
        QCOMPARE(m_client->machineStacks, QList<QcItemStack>(3));
        chat(u"/give coal 1"_s);
        chat(u"/give dirt 2"_s);
        // Dirt is no fuel, and nothing goes into the output.
        move(slotOf(item("qtcraft:dirt")), fuel);
        QCOMPARE(countOf(item("qtcraft:dirt")), 2);
        move(slotOf(item("qtcraft:raw_iron")), output);
        QCOMPARE(countOf(item("qtcraft:raw_iron")), 3);
        QVERIFY(m_client->machineStacks[2].isEmpty());

        move(slotOf(item("qtcraft:raw_iron")), input);
        move(slotOf(item("qtcraft:coal")), fuel);
        QCOMPARE(countOf(item("qtcraft:raw_iron")), 0);
        // Raw iron takes 10 s (content/qtcraft/smelting/iron_ingot.json); one coal burns 80 s.
        ticks(10 * int(QcPhysics::tickRate) + 5);
        QCOMPARE(m_client->machineStacks[0], (QcItemStack{item("qtcraft:raw_iron"), 2, 0}));
        QVERIFY(m_client->machineStacks[1].isEmpty());
        QCOMPARE(m_client->machineStacks[2], (QcItemStack{item("qtcraft:iron_ingot"), 1, 0}));
        QVERIFY(m_client->machineBurn > 0.8f && m_client->machineBurn < 0.9f);

        // Swapping the output with a stack would put that stack into the output: refused.
        move(output, slotOf(item("qtcraft:dirt")));
        QCOMPARE(countOf(item("qtcraft:dirt")), 2);
        QCOMPARE(m_client->machineStacks[2].item, item("qtcraft:iron_ingot"));
        // Taking it into an empty slot works.
        move(output, 20);
        QCOMPARE(countOf(item("qtcraft:iron_ingot")), 1);
        QVERIFY(m_client->machineStacks[2].isEmpty());

        // After closing, the machine slots are invalid again.
        send(QcMsg::CloseMachine{});
        QVERIFY(!m_server->blockEntities().machine(pos)->stacks[0].isEmpty());
        move(input, 21);
        QCOMPARE(countOf(item("qtcraft:raw_iron")), 0);
        move(slotOf(item("qtcraft:dirt")), fuel);
        QCOMPARE(countOf(item("qtcraft:dirt")), 2);
    }

    void anEmptiedBucketInTheFuelSlotKeepsTheScreenUsable()
    {
        join();
        const QcBlockPos pos = openFurnace();
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(pos));

        chat(u"/give raw_iron 2"_s);
        chat(u"/give lava_bucket 1"_s);
        chat(u"/give dirt 3"_s);
        move(slotOf(item("qtcraft:raw_iron")), input);
        move(slotOf(item("qtcraft:lava_bucket")), fuel);
        QCOMPARE(countOf(item("qtcraft:lava_bucket")), 0);
        ticks(10 * int(QcPhysics::tickRate) + 5);

        // The lava burns, the empty bucket stays in the fuel slot.
        QCOMPARE(m_client->machineStacks[1], (QcItemStack{item("qtcraft:bucket"), 1, 0}));
        QCOMPARE(m_client->machineStacks[2], (QcItemStack{item("qtcraft:iron_ingot"), 1, 0}));

        // Taking the output and moving inventory items still works.
        move(output, 20);
        QCOMPARE(countOf(item("qtcraft:iron_ingot")), 1);
        QVERIFY(m_client->machineStacks[2].isEmpty());

        const int dirt = slotOf(item("qtcraft:dirt"));
        move(dirt, 25);
        QCOMPARE(m_client->stacks[25], (QcItemStack{item("qtcraft:dirt"), 3, 0}));

        chat(u"/give raw_iron 1"_s);
        move(slotOf(item("qtcraft:raw_iron")), input);
        QCOMPARE(m_client->machineStacks[0], (QcItemStack{item("qtcraft:raw_iron"), 2, 0}));

        // Non-fuel cannot be put next to the bucket or swapped in for it.
        move(25, fuel);
        QCOMPARE(m_client->stacks[25], (QcItemStack{item("qtcraft:dirt"), 3, 0}));
        QCOMPARE(m_client->machineStacks[1], (QcItemStack{item("qtcraft:bucket"), 1, 0}));

        // The bucket can be taken out, but not put back.
        move(fuel, 26);
        QCOMPARE(m_client->stacks[26], (QcItemStack{item("qtcraft:bucket"), 1, 0}));
        QVERIFY(m_client->machineStacks[1].isEmpty());
        move(26, fuel);
        QCOMPARE(m_client->stacks[26], (QcItemStack{item("qtcraft:bucket"), 1, 0}));
        QVERIFY(m_client->machineStacks[1].isEmpty());
    }

    void machineScreensNeedReachAndCloseFromAfar()
    {
        join();
        // Out of reach: nothing opens.
        chat(u"/setblock ~9 ~ ~ furnace"_s);
        QCOMPARE(m_server->world().blockAt(m_feet.offset(9, 0, 0)), std::optional<QcBlockId>(id("qtcraft:furnace")));
        send(QcMsg::UseItem{m_feet.offset(9, 0, 0), QcBlockFace::West});
        QVERIFY(!m_client->openMachine);
        // Sneaking places against the furnace instead of opening it.
        const QcBlockPos pos = placeFurnace();
        chat(u"/give dirt 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:dirt"))});
        QcPlayerInput sneak;
        sneak.sequence = ++m_sequence;
        sneak.sneak = true;
        send(QcMsg::PlayerInput{sneak});
        send(QcMsg::UseItem{pos, QcBlockFace::Up});
        QVERIFY(!m_client->openMachine);
        QCOMPARE(m_server->world().blockAt(pos.offset(0, 1, 0)), std::optional<QcBlockId>(id("qtcraft:dirt")));

        // Open it, then walk 12 m west along a cleared path: the screen closes.
        idle(1);
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        QCOMPARE(m_client->openMachine, std::optional<QcBlockPos>(pos));
        for (int dx = -1; dx >= -14; --dx) {
            m_server->world().setBlock(m_feet.offset(dx, -1, 0), id("qtcraft:grass_block"));
            for (int dy = 0; dy <= 2; ++dy) {
                m_server->world().setBlock(m_feet.offset(dx, dy, 0), qcAirBlockId);
            }
        }
        for (int i = 0; i < 60 && !m_client->machineClosed; ++i) {
            QcPlayerInput walk;
            walk.sequence = ++m_sequence;
            walk.yaw = 270.0f;
            walk.forward = 1.0f;
            send(QcMsg::PlayerInput{walk});
        }
        QVERIFY(m_client->machineClosed);
        QVERIFY(m_client->motion.position.x < pos.x - 5.0);
    }

    void breakingAFurnaceDropsItsContents()
    {
        join();
        const QcBlockPos pos = openFurnace();

        chat(u"/give raw_iron 3"_s);
        chat(u"/give coal 2"_s);
        chat(u"/give wooden_pickaxe"_s);
        move(slotOf(item("qtcraft:raw_iron")), input);
        move(slotOf(item("qtcraft:coal")), fuel);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:wooden_pickaxe"))});

        // Hardness 3.5 x 1.5 s / wooden pickaxe speed 2 = 2.6 s; one coal lights meanwhile.
        m_client->connection.send(QcMsg::StartMining{pos});
        QcTestUtil::pump();
        ticks(60);
        m_client->connection.send(QcMsg::FinishMining{pos});
        QcTestUtil::pump();
        ticks(1);

        QCOMPARE(m_server->world().blockAt(pos), std::optional<QcBlockId>(qcAirBlockId));
        QVERIFY(!m_server->blockEntities().machine(pos));
        QVERIFY(m_client->machineClosed);
        QCOMPARE(droppedCount(item("qtcraft:raw_iron")), 3);
        QCOMPARE(droppedCount(item("qtcraft:coal")), 1);
        QCOMPARE(droppedCount(item("qtcraft:furnace")), 1);
    }

    void machinesSurviveARestart()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);
        const QcBlockPos pos = openFurnace();
        chat(u"/give raw_iron 3"_s);
        chat(u"/give coal 2"_s);
        move(slotOf(item("qtcraft:raw_iron")), input);
        move(slotOf(item("qtcraft:coal")), fuel);
        const QcBlockEntities::Machine before = *m_server->blockEntities().machine(pos);
        QCOMPARE(before.stacks[1].count, 1);
        QVERIFY(before.burnRemaining > 0.0);
        QVERIFY(m_server->stop().has_value());
        // The furnace is kept with its chunk, in the block-entity file of its region (decision E-358).
        const QcChunkPos region = QcWorldStorage::regionOf(pos.chunk());
        const QString path = dir.filePath(u"blockentities/r.%1.%2.qcb"_s.arg(region.x).arg(region.z));
        QVERIFY(QFile::exists(path));
        QVERIFY(!QFile::exists(dir.filePath(u"machines.qce"_s)));
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(pos);
        const QcBlockEntities::Machine *after = m_server->blockEntities().machine(pos);
        QVERIFY(after);
        QCOMPARE(after->stacks, before.stacks);
        QCOMPARE(after->burnRemaining, before.burnRemaining);
        QCOMPARE(after->burnSeconds, 80.0);
        QCOMPARE(after->progress, before.progress);

        // A damaged block-entity file stops the world from opening with a message naming it.
        QVERIFY(m_server->stop().has_value());
        m_server.reset();
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        file.resize(file.size() - 4);
        file.close();
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        const QcStatus opened = m_server->openWorld();
        QVERIFY(!opened.has_value());
        QVERIFY2(opened.error().contains(path), qPrintable(opened.error()));
    }

    void crowdedCraftingGridSpillsOntoTheGround()
    {
        join();
        chat(u"/give oak_log 4"_s);
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:oak_log")), QcServer::craftingGridSlot, 0});
        chat(u"/give stone 2304"_s);
        QCOMPARE(countOf(item("qtcraft:stone")), 36 * 64);
        send(QcMsg::CloseCrafting{});
        QCOMPARE(countOf(item("qtcraft:oak_log")), 0);
        QCOMPARE(droppedCount(item("qtcraft:oak_log")), 4);
    }

    void stoppingEmptiesOpenGridsWithoutRepairingTools_data()
    {
        QTest::addColumn<bool>("disconnectFirst");
        QTest::newRow("connected") << false;
        // The game closes the connection before it stops the server.
        QTest::newRow("disconnected") << true;
    }

    void stoppingEmptiesOpenGridsWithoutRepairingTools()
    {
        QFETCH(bool, disconnectFirst);
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 0;
        join(config);

        chat(u"/give stone_pickaxe"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:stone_pickaxe"))});

        // Wear the pickaxe once on the grass next to the player.
        const QcBlockPos grass = m_feet.offset(1, -1, 0);
        send(QcMsg::StartMining{grass});
        ticks(40);
        send(QcMsg::FinishMining{grass});
        QCOMPARE(m_server->world().blockAt(grass), std::optional<QcBlockId>(qcAirBlockId));
        send(QcMsg::MoveInventoryItem{slotOf(item("qtcraft:stone_pickaxe")), QcServer::craftingGridSlot, 0});
        QCOMPARE(m_client->craftingGrid.first().damage, 1);

        // A full inventory: the pickaxe cannot go back and must land on the ground, still worn.
        chat(u"/give stone 2304"_s);
        if (disconnectFirst) {
            m_client->connection.close();
            QcTestUtil::pump();
        }

        QVERIFY(m_server->stop().has_value());
        m_client.reset();
        config.viewDistance = 1;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        loadAround(m_feet);

        int pickaxes = 0;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            if (dropped.stack.item == item("qtcraft:stone_pickaxe")) {
                ++pickaxes;
                QCOMPARE(dropped.stack.damage, 1);
            }
        }
        QCOMPARE(pickaxes, 1);
    }
};

QTEST_GUILESS_MAIN(TestServerCrafting)
#include "test_server_crafting.moc"

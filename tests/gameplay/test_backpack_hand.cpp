#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcPortableContainers.h"
#include "common/gameplay/QcRepair.h"
#include "QcServerTestFixture.h"
#include "QcTestUtil.h"


using namespace Qt::StringLiterals;

// The backpack opened from the hand and the rules that keep its contents whole (decisions E-832,
// E-833): used in the air it shows its row and the hotbar, its own slot locked; nothing with
// contents goes into a backpack by click or shift-click; stacks with contents are never paid,
// crafted, smelted or burnt away; refused data leaves a stack as it was (review points R3, R5 of
// package 68.1).
class TestBackpackHand : public QcServerTestFixture
{
    Q_OBJECT

    static constexpr int block = QcMsg::MoveInventoryItem::machineInputSlot;

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    QcItemStack stackOf(const char *name, int count) const { return {item(name), count, 0}; }
    QcItemStack backpack() const { return stackOf("qtcraft:backpack", 1); }
    QcItemStack packed(QList<QcItemStack> contents) const { return backpack().withContents(std::move(contents)); }

    QList<QcItemStack> padded(QList<QcItemStack> stacks) const
    {
        stacks.resize(9);
        return stacks;
    }

    // Backpacks in the inventory, filled ones too (QcInventory::countOf leaves those out).
    qsizetype backpacksHeld()
    {
        const QList<QcItemStack> stacks = player().inventory.allSlots();
        return std::ranges::count_if(stacks, [this](const QcItemStack &stack) { return !stack.isEmpty() && stack.item == item("qtcraft:backpack"); });
    }

    // Holds `stack` in hotbar slot `slot` and uses it in the air.
    void openInHand(const QcItemStack &stack, int slot = 2)
    {
        player().inventory.setSlot(slot, stack);
        send(QcMsg::SelectHotbarSlot{slot});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(m_client->openMachineHeldSlot, slot);
    }

    // Places a backpack two east of the feet and opens it.
    QcBlockPos openPlaced()
    {
        const QcBlockPos pos = m_feet.offset(2, 0, 0);
        player().inventory.setSlot(8, backpack());
        send(QcMsg::SelectHotbarSlot{8});
        send(QcMsg::UseItem{pos.offset(0, -1, 0), QcBlockFace::Up});
        send(QcMsg::UseItem{pos, QcBlockFace::West});
        return pos;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void openFromTheHandShowsItsSlots()
    {
        join();
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 3)};
        openInHand(packed(inside));
        QCOMPARE(m_client->machineCategory, QString());
        QCOMPARE(m_client->machineStacks, padded(inside));

        // Into the backpack from the hotbar: the item carries it, the client sees it at once.
        player().inventory.setSlot(0, stackOf("qtcraft:cobblestone", 10));
        move(0, block + 1);
        const QList<QcItemStack> after{stackOf("qtcraft:dirt", 5), stackOf("qtcraft:cobblestone", 10), stackOf("qtcraft:stick", 3)};
        QCOMPARE(player().inventory.slot(2), packed(after));
        QCOMPARE(m_client->stacks.value(2), packed(after));
        QCOMPARE(m_client->machineStacks, padded(after));
        QVERIFY(player().inventory.slot(0).isEmpty());

        // Out again with a shift-click: into the hotbar only.
        for (int slot = 3; slot < QcInventory::hotbarSize; ++slot) {
            player().inventory.setSlot(slot, stackOf("qtcraft:oak_planks", 64));
        }
        send(QcMsg::QuickMoveItem{block});
        QCOMPARE(player().inventory.slot(0), stackOf("qtcraft:dirt", 5));
        send(QcMsg::QuickMoveItem{block + 2});
        QCOMPARE(player().inventory.slot(1), stackOf("qtcraft:stick", 3));
        // The hotbar is full now: the rest stays, nothing goes to the storage rows.
        send(QcMsg::QuickMoveItem{block + 1});
        QCOMPARE(player().inventory.slot(2), packed({{}, stackOf("qtcraft:cobblestone", 10)}));
        for (int slot = QcInventory::hotbarSize; slot < QcInventory::slotCount; ++slot) {
            QVERIFY(player().inventory.slot(slot).isEmpty());
        }

        send(QcMsg::CloseMachine{});
        QVERIFY(!player().screen.held);
    }

    void itsSlotAndTheStorageRowsAreLocked()
    {
        join();
        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        const QcItemStack held = player().inventory.slot(2);
        player().inventory.setSlot(20, stackOf("qtcraft:stick", 4));
        player().inventory.setSlot(0, stackOf("qtcraft:cobblestone", 1));

        move(2, block + 3);
        move(block, 2);
        move(0, 2);
        move(20, block + 4);
        move(block, 20);
        send(QcMsg::QuickMoveItem{2});
        send(QcMsg::DropItem{true});
        QCOMPARE(player().inventory.slot(2), held);
        QCOMPARE(player().inventory.slot(20), stackOf("qtcraft:stick", 4));
        QCOMPARE(player().inventory.slot(0), stackOf("qtcraft:cobblestone", 1));
        QCOMPARE(m_server->items().items().size(), 0);
        QVERIFY(player().screen.held);
    }

    void noBackpackGoesIntoABackpack()
    {
        join();
        openInHand(backpack());
        player().inventory.setSlot(0, backpack());
        player().inventory.setSlot(1, packed({stackOf("qtcraft:dirt", 1)}));
        move(0, block);
        move(1, block + 1);
        send(QcMsg::QuickMoveItem{0});
        QCOMPARE(player().inventory.slot(2), backpack());
        QCOMPARE(player().inventory.slot(0), backpack());
        QCOMPARE(player().inventory.slot(1), packed({stackOf("qtcraft:dirt", 1)}));
        send(QcMsg::CloseMachine{});

        // The same for a placed one, also by shift-click from the storage rows.
        openPlaced();
        QCOMPARE(m_client->openMachineHeldSlot, -1);
        QVERIFY(m_client->openMachine);
        player().inventory.setSlot(20, backpack());
        move(0, block);
        send(QcMsg::QuickMoveItem{20});
        const QcBlockPos pos = m_feet.offset(2, 0, 0);
        QCOMPARE(m_server->blockEntities().container(pos)->stacks, QList<QcItemStack>(9));
        // Other items go in as into a chest.
        player().inventory.setSlot(21, stackOf("qtcraft:dirt", 3));
        send(QcMsg::QuickMoveItem{21});
        QCOMPARE(m_server->blockEntities().container(pos)->stacks.at(0), stackOf("qtcraft:dirt", 3));
    }

    void theScreenClosesWhenTheBackpackLeavesTheHand()
    {
        QcServerConfig config;
        config.keepInventory = false;
        join(config);
        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        player().inventory.setSlot(2, {});
        ticks(1);
        QVERIFY(m_client->machineClosed);
        QVERIFY(!player().screen.held);

        // Dying drops the backpack with its contents and closes the screen.
        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        chat(u"/kill"_s);
        ticks(1);
        QVERIFY(m_client->machineClosed);
        int dropped = 0;
        for (const QcItemEntity &entity : m_server->items().items()) {
            dropped += entity.stack == packed({stackOf("qtcraft:dirt", 5)}) ? 1 : 0;
        }
        QCOMPARE(dropped, 1);
    }

    void creativeCannotReplaceTheLockedSlot()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        send(QcMsg::CreativeItem{2, stackOf("qtcraft:stone", 1)});
        send(QcMsg::CreativeItem{2, {}});
        QCOMPARE(player().inventory.slot(2), packed({stackOf("qtcraft:dirt", 5)}));
    }

    void contentsTheSlotsCannotShowGoToThePlayer()
    {
        join();
        // Beyond the nine slots, and a backpack inside: the screen cannot show them, so they must
        // not vanish with the first click.
        QList<QcItemStack> inside(11);
        inside[0] = stackOf("qtcraft:dirt", 5);
        inside[1] = backpack();
        inside[9] = stackOf("qtcraft:cobblestone", 10);
        inside[10] = stackOf("qtcraft:cobblestone", 20);
        openInHand(packed(inside));
        QCOMPARE(m_client->machineStacks, padded({stackOf("qtcraft:dirt", 5)}));
        const QcItemStack held = player().inventory.slot(2);
        QCOMPARE(QcPortableContainers::slotsOf(*m_content, held, 9), padded({stackOf("qtcraft:dirt", 5)}));
        QVERIFY(QcPortableContainers::leftOver(*m_content, held, 9).isEmpty());
        QCOMPARE(player().inventory.countOf(item("qtcraft:cobblestone")), 30);
        QCOMPARE(backpacksHeld(), 2);

        // A click into the screen keeps everything.
        player().inventory.setSlot(5, stackOf("qtcraft:stick", 1));
        move(5, block + 1);
        QCOMPARE(QcPortableContainers::slotsOf(*m_content, player().inventory.slot(2), 9), padded({stackOf("qtcraft:dirt", 5), stackOf("qtcraft:stick", 1)}));
        QCOMPARE(player().inventory.countOf(item("qtcraft:cobblestone")), 30);
        QCOMPARE(backpacksHeld(), 2);
    }

    void aHeldScreenTellsNoBlockOfItsChanges()
    {
        join();
        // A chest at the world origin with a viewer: a held backpack is no block there.
        m_server->world().loadChunk(QcBlockPos{0, 0, 0}.chunk());
        QVERIFY(m_server->world().setBlock({0, 0, 0}, id("qtcraft:chest")));
        QVERIFY(ensureEntity({0, 0, 0}));
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), m_origin);
        RawClient other(std::move(clientEnd));
        other.hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);
        QcServerClient &viewer = *m_server->entities().clients().back();
        QCOMPARE(viewer.name, u"Other"_s);

        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        player().inventory.setSlot(0, stackOf("qtcraft:stick", 1));
        // Looking into the chest from afar (the screens close out of reach at the end of the tick).
        viewer.screen.open = QcBlockPos{0, 0, 0};
        viewer.screen.sent.reset();
        other.machineStacks.clear();
        move(0, block + 1);
        QcTestUtil::pump();
        QCOMPARE(m_client->machineStacks, padded({stackOf("qtcraft:dirt", 5), stackOf("qtcraft:stick", 1)}));
        QVERIFY(other.machineStacks.isEmpty());
    }

    void pickBlockKeepsTheLockedSlot()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        for (int slot = 0; slot < QcInventory::hotbarSize; ++slot) {
            player().inventory.setSlot(slot, stackOf("qtcraft:oak_planks", 1));
        }
        openInHand(packed({stackOf("qtcraft:dirt", 5)}));
        send(QcMsg::PickBlock{m_feet.offset(1, -1, 0)});
        QCOMPARE(player().inventory.slot(2), packed({stackOf("qtcraft:dirt", 5)}));
        QCOMPARE(player().inventory.selectedHotbarSlot(), 2);
        QCOMPARE(player().inventory.countOf(item("qtcraft:grass_block")), 0);
    }

    void gameModesOpenAsForOtherContainers_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("opens");
        QTest::addRow("survival") << static_cast<int>(QcGameMode::Survival) << true;
        QTest::addRow("adventure") << static_cast<int>(QcGameMode::Adventure) << true;
        QTest::addRow("creative") << static_cast<int>(QcGameMode::Creative) << true;
        QTest::addRow("spectator") << static_cast<int>(QcGameMode::Spectator) << false;
    }

    void gameModesOpenAsForOtherContainers()
    {
        QFETCH(int, mode);
        QFETCH(bool, opens);
        QcServerConfig config;
        config.defaultGameMode = static_cast<QcGameMode>(mode);
        join(config);
        player().inventory.setSlot(2, packed({stackOf("qtcraft:dirt", 5)}));
        send(QcMsg::SelectHotbarSlot{2});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(player().screen.held.has_value(), opens);
        QCOMPARE(m_client->openMachineHeldSlot, opens ? 2 : -1);
        QCOMPARE(player().inventory.slot(2), packed({stackOf("qtcraft:dirt", 5)}));
    }

    void anItemThatIsNoBackpackDoesNotOpen()
    {
        join();
        player().inventory.setSlot(2, stackOf("qtcraft:dirt", 1));
        send(QcMsg::SelectHotbarSlot{2});
        send(QcMsg::ConsumeItem{});
        QVERIFY(!m_client->openMachine);
        QVERIFY(!player().screen.held);
    }

    void refusedDataLeavesTheStackAsItWas()
    {
        // More slots than any contents may hold: nothing happens, and the caller knows.
        const QcItemStack full = packed({stackOf("qtcraft:dirt", 5)});
        QList<QcItemStack> tooMany(QcItemData::maxContents + 1, stackOf("qtcraft:stick", 1));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Refused item data"_s));
        QVERIFY(!full.tryWithContents(tooMany));
        QCOMPARE(full.contents(), QList<QcItemStack>{stackOf("qtcraft:dirt", 5)});
        QCOMPARE(full.tryWithContents({stackOf("qtcraft:stick", 2)}), packed({stackOf("qtcraft:stick", 2)}));
    }

    void stacksWithContentsAreNeverUsedUp()
    {
        QcInventory inventory;
        inventory.setSlot(0, packed({stackOf("qtcraft:dirt", 5)}));
        inventory.setSlot(1, backpack());
        QCOMPARE(inventory.countOf(item("qtcraft:backpack")), 1);
        QVERIFY(!inventory.removeItems(item("qtcraft:backpack"), 2));
        QVERIFY(inventory.removeItems(item("qtcraft:backpack"), 1));
        QCOMPARE(inventory.slot(0), packed({stackOf("qtcraft:dirt", 5)}));
        QVERIFY(inventory.slot(1).isEmpty());

        // No recipe takes a filled stack.
        QList<QcItemStack> grid(9, stackOf("qtcraft:leather", 1));
        grid[4] = stackOf("qtcraft:chest", 1);
        QVERIFY(QcCrafting::match(*m_content, grid, 3));
        grid[0] = packed({stackOf("qtcraft:dirt", 1)});
        QVERIFY(!QcCrafting::match(*m_content, grid, 3));
    }

    void repairKeepsTheName()
    {
        const QcItemId pickaxe = item("qtcraft:iron_pickaxe");
        const QList<QcItemStack> grid{QcItemStack{pickaxe, 1, 100}.withName(u"Digger"_s), QcItemStack{pickaxe, 1, 50}};
        const std::optional<QcItemStack> repaired = QcRepair::result(*m_content, grid);
        QVERIFY(repaired);
        QCOMPARE(repaired->name(), u"Digger"_s);
        QVERIFY(repaired->damage < 50);
    }

    void stacksSortWithTheirData()
    {
        const QcItemStack a = stackOf("qtcraft:stick", 1).withName(u"A"_s);
        const QcItemStack b = stackOf("qtcraft:stick", 1).withName(u"B"_s);
        QVERIFY(QcItemStack::compare(a, b) < 0);
        QVERIFY(QcItemStack::compare(b, a) > 0);
        QCOMPARE(QcItemStack::compare(a, stackOf("qtcraft:stick", 1).withName(u"A"_s)), 0);
        QVERIFY(QcItemStack::compare(packed({stackOf("qtcraft:dirt", 1)}), packed({stackOf("qtcraft:dirt", 2)})) < 0);
        // Empty slots sort first.
        QVERIFY(QcItemStack::compare({}, a) < 0);
    }

    void aFilledBackpackIsNeitherFuelNorSmelted()
    {
        join();
        const QcBlockPos furnace = openFurnace();
        // A chest is fuel; a backpack is none, even with contents.
        player().inventory.setSlot(0, packed({stackOf("qtcraft:oak_planks", 1)}));
        move(0, fuel);
        QVERIFY(m_server->blockEntities().machine(furnace)->stacks[QcBlockEntities::Machine::fuelSlot].isEmpty());
        move(0, input);
        player().inventory.setSlot(1, stackOf("qtcraft:coal", 4));
        move(1, fuel);
        ticks(100);
        QCOMPARE(m_server->blockEntities().machine(furnace)->stacks[QcBlockEntities::Machine::inputSlot], packed({stackOf("qtcraft:oak_planks", 1)}));
        QVERIFY(m_server->blockEntities().machine(furnace)->stacks[QcBlockEntities::Machine::outputSlot].isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestBackpackHand)
#include "test_backpack_hand.moc"

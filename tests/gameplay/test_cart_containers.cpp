// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "QcRailFixture.h"
#include "QcTestUtil.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

// Chest and hopper carts (stage 32.2, decisions E-386, E-387): a player places a chest cart, opens its
// screen, fills it and breaks it (it spills what it carried), also while two players look into it; container carts leave the world with
// their chunk and come back with their slots; a hopper cart pulls from a chest above it and picks up
// items; a Signal Hopper under the rail unloads chest and hopper carts into the chest it faces; the
// carts keep within the hoppers' budget. Placements in all four rotations.
class TestCartContainers : public QcRailFixture
{
    Q_OBJECT

private:
    // A straight line of rails along local z = `z` from x = 1 to 12.
    void track(int z)
    {
        QList<QcBlockPos> cells;
        for (int x = 1; x <= 12; ++x) {
            cells.append({x, 0, z});
        }
        lay(cells);
    }

    // A chest at local (x, y, z) holding `count` cobblestone.
    void chestWith(int x, int y, int z, int count)
    {
        put(x, y, z, u"chest"_s);
        QVERIFY(ensureEntity(at(x, y, z)));
        m_server->blockEntities().container(at(x, y, z))->stacks[0] = {item("qtcraft:cobblestone"), count, 0};
    }

    int itemsIn(const QList<QcItemStack> &stacks) const
    {
        int total = 0;
        for (const QcItemStack &stack : stacks) {
            total += stack.count;
        }
        return total;
    }

    int chestCount(int x, int y, int z) const
    {
        const QcBlockEntities::Container *container = m_server->blockEntities().container(at(x, y, z));
        return container ? itemsIn(container->stacks) : 0;
    }

    QString turnText() const { return u" (turns %1)"_s.arg(m_turns); }

    // Another player joining at the spawn.
    std::unique_ptr<RawClient> joinAnother(const QString &name)
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        auto client = std::make_unique<RawClient>(std::move(clientEnd));
        client->hello(name);
        QcTestUtil::pump();
        ticks(2);
        return client;
    }

private slots:
    void playerFillsAndBreaksAChestCart()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/give rail 4"_s);
        chat(u"/give chest_minecart"_s);
        chat(u"/give cobblestone 10"_s);
        ticks(2);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:rail"))});
        for (int x = 1; x <= 3; ++x) {
            send(QcMsg::UseItem{m_feet.offset(x, -1, 0), QcBlockFace::Up});
        }
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:chest_minecart"))});
        send(QcMsg::UseItem{m_feet.offset(2, 0, 0), QcBlockFace::Up});
        QCOMPARE(m_server->carts().all().size(), 1);
        const quint32 id = m_server->carts().all().constFirst().id;
        QCOMPARE(cart(id).stacks.size(), 27);

        // A use opens its screen (no ride); the cobblestone goes into its first slot.
        send(QcMsg::UseEntity{id});
        idle(1);
        QCOMPARE(m_client->openMachineEntity, id);
        QCOMPARE(cart(id).rider, 0u);
        QCOMPARE(m_client->machineStacks.size(), 27);
        move(slotOf(item("qtcraft:cobblestone")), inputSlot);
        idle(1);
        QCOMPARE(cart(id).stacks[0], (QcItemStack{item("qtcraft:cobblestone"), 10, 0}));
        QCOMPARE(m_client->machineStacks[0].count, 10);
        QCOMPARE(countOf(item("qtcraft:cobblestone")), 0);

        // Broken, it drops itself and what it carried; the screen closes.
        idle(10);
        send(QcMsg::AttackEntity{id});
        idle(1);
        QVERIFY(m_server->carts().all().isEmpty());
        QVERIFY(m_client->machineClosed);
        QCOMPARE(droppedCount(item("qtcraft:chest_minecart")), 1);
        QCOMPARE(droppedCount(item("qtcraft:cobblestone")), 10);
        QVERIFY(entityProblems().isEmpty());
    }

    void twoPlayersLookIntoAChestCartWhileAThirdBreaksIt()
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/give rail 4"_s);
        chat(u"/give chest_minecart"_s);
        chat(u"/give cobblestone 10"_s);
        ticks(2);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:rail"))});
        for (int x = 1; x <= 3; ++x) {
            send(QcMsg::UseItem{m_feet.offset(x, -1, 0), QcBlockFace::Up});
        }
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:chest_minecart"))});
        send(QcMsg::UseItem{m_feet.offset(2, 0, 0), QcBlockFace::Up});
        const quint32 id = m_server->carts().all().constFirst().id;
        send(QcMsg::UseEntity{id});
        move(slotOf(item("qtcraft:cobblestone")), inputSlot);
        idle(1);
        QCOMPARE(cart(id).stacks[0].count, 10);

        // A second player looks in too and sees the cobblestone; a third breaks the cart.
        std::unique_ptr<RawClient> bob = joinAnother(u"Bob"_s);
        bob->connection.send(QcMsg::UseEntity{id});
        QcTestUtil::pump();
        idle(1);
        QCOMPARE(bob->openMachineEntity, id);
        QCOMPARE(bob->machineStacks.value(0).count, 10);
        std::unique_ptr<RawClient> cy = joinAnother(u"Cy"_s);
        cy->connection.send(QcMsg::AttackEntity{id});
        QcTestUtil::pump();
        idle(1);

        // Both screens close, the cobblestone lies on the ground once, and a click into the closed
        // screen takes nothing out of it.
        QVERIFY(m_server->carts().all().isEmpty());
        QVERIFY(m_client->machineClosed);
        QVERIFY(bob->machineClosed);
        QCOMPARE(droppedCount(item("qtcraft:cobblestone")), 10);
        QCOMPARE(droppedCount(item("qtcraft:chest_minecart")), 1);
        bob->connection.send(QcMsg::MoveInventoryItem{inputSlot, 0, 0});
        move(inputSlot, 0);
        QcTestUtil::pump();
        idle(1);
        QCOMPARE(countOf(item("qtcraft:cobblestone")), 0);
        QVERIFY(std::ranges::none_of(bob->stacks, [this](const QcItemStack &stack) { return stack.item == item("qtcraft:cobblestone"); }));
        QCOMPARE(droppedCount(item("qtcraft:cobblestone")), 10);
        QVERIFY(entityProblems().isEmpty());
    }

    void containerCartsLeaveWithTheirChunkAndComeBack()
    {
        m_unloadChunks = true;
        buildArea();
        const QcBlockPos far = m_feet.offset(6 * QcWorld::chunkSize, 0, 0);
        clearArea(far);
        lay({{3, 0, 3}, {4, 0, 3}, {5, 0, 3}, {6, 0, 3}});
        const quint32 chest = cartOf(u"qtcraft:chest_minecart"_s, 4, 0, 3);
        m_server->carts().find(chest)->stacks[0] = {item("qtcraft:cobblestone"), 33, 0};
        m_server->carts().find(chest)->stacks[26] = {item("qtcraft:iron_ingot"), 5, 0};
        const quint32 hopper = cartOf(u"qtcraft:hopper_minecart"_s, 6, 0, 3);
        m_server->carts().find(hopper)->stacks[4] = {item("qtcraft:stick"), 7, 0};
        m_server->carts().find(hopper)->enabled = false;
        const QList<QcItemStack> chestSlots = cart(chest).stacks;
        const QList<QcItemStack> hopperSlots = cart(hopper).stacks;

        const QcChunkPos chunk = QcPhysics::blockContaining(cart(chest).motion.position).chunk();
        for (int i = 0; i < 200 && m_server->world().chunk(chunk); ++i) {
            ticks(1);
        }
        QVERIFY(!m_server->world().chunk(chunk));
        QVERIFY(m_server->carts().all().isEmpty());

        m_server->world().loadChunk(chunk);
        QCOMPARE(m_server->carts().all().size(), 2);
        const auto back = [this](const QString &name) {
            return *std::ranges::find(m_server->carts().all(), name, &QcMinecart::item);
        };
        QCOMPARE(back(u"qtcraft:chest_minecart"_s).stacks, chestSlots);
        QCOMPARE(back(u"qtcraft:hopper_minecart"_s).stacks, hopperSlots);
        QVERIFY(!back(u"qtcraft:hopper_minecart"_s).enabled);
        QVERIFY(back(u"qtcraft:chest_minecart"_s).enabled);
        QVERIFY(entityProblems().isEmpty());
        m_unloadChunks = false;
    }

    void hopperCartPullsFromAChestAboveAndPicksUpItems()
    {
        const int cooldown = QcHopperQueue{}.cooldownTicks;
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            track(4);
            chestWith(4, 1, 4, 3);
            const quint32 id = cartOf(u"qtcraft:hopper_minecart"_s, 4, 0, 4);
            ticks(1);
            QCOMPARE(itemsIn(cart(id).stacks), 1);
            // One item per cooldown.
            ticks(cooldown - 1);
            QCOMPARE(itemsIn(cart(id).stacks), 1);
            ticks(2 * cooldown + 1);
            QVERIFY2(itemsIn(cart(id).stacks) == 3, qPrintable(QString::number(itemsIn(cart(id).stacks)) + turnText()));
            QCOMPARE(chestCount(4, 1, 4), 0);

            // Items lying on the rail of a cart without a chest above it go in whole.
            const quint32 other = cartOf(u"qtcraft:hopper_minecart"_s, 9, 0, 4);
            m_server->items().spawn({{item("qtcraft:stick"), 12, 0}, point(9.5, 0.1, 4.5), {}}, m_server->entities().newId());
            ticks(3);
            QVERIFY2(itemsIn(cart(other).stacks) == 12, qPrintable(QString::number(itemsIn(cart(other).stacks)) + turnText()));
            QCOMPARE(droppedCount(item("qtcraft:stick")), 0);
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void hoppersUnderTheRailUnloadCarts()
    {
        for (m_turns = 0; m_turns < 4; ++m_turns) {
            buildArea();
            // Hoppers in the floor under the rails, facing east into chests.
            put(4, -1, 4, u"signal_hopper[facing=east]"_s);
            put(5, -1, 4, u"chest"_s);
            put(4, -1, 8, u"signal_hopper[facing=east]"_s);
            put(5, -1, 8, u"chest"_s);
            track(4);
            track(8);
            const quint32 chest = cartOf(u"qtcraft:chest_minecart"_s, 4, 0, 4);
            m_server->carts().find(chest)->stacks[3] = {item("qtcraft:cobblestone"), 4, 0};
            const quint32 hopper = cartOf(u"qtcraft:hopper_minecart"_s, 4, 0, 8);
            m_server->carts().find(hopper)->stacks[0] = {item("qtcraft:stick"), 3, 0};
            QVERIFY(isBlock(4, 0, 4, "rail"));

            ticks(120);
            QVERIFY2(itemsIn(cart(chest).stacks) == 0, qPrintable(QString::number(itemsIn(cart(chest).stacks)) + turnText()));
            QCOMPARE(itemsIn(cart(hopper).stacks), 0);
            QCOMPARE(chestCount(5, -1, 4), 4);
            QCOMPARE(chestCount(5, -1, 8), 3);
            QVERIFY(entityProblems().isEmpty());
        }
    }

    void cartHoppersKeepWithinTheBudget()
    {
        QcServerConfig config;
        config.limits.hopperBudget = 1;
        buildArea(config);
        track(4);
        track(8);
        chestWith(4, 1, 4, 5);
        chestWith(4, 1, 8, 5);
        const quint32 first = cartOf(u"qtcraft:hopper_minecart"_s, 4, 0, 4);
        const quint32 second = cartOf(u"qtcraft:hopper_minecart"_s, 4, 0, 8);
        ticks(1);
        QCOMPARE(itemsIn(cart(first).stacks) + itemsIn(cart(second).stacks), 1);
        ticks(1);
        // The next tick goes on with the other cart.
        QCOMPARE(itemsIn(cart(first).stacks), 1);
        QCOMPARE(itemsIn(cart(second).stacks), 1);
    }
};

QTEST_GUILESS_MAIN(TestCartContainers)
#include "test_cart_containers.moc"

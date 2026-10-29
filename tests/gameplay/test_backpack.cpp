// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcCrafting.h"
#include "common/gameplay/QcMining.h"
#include "common/gameplay/QcPortableContainers.h"
#include "common/physics/QcPhysics.h"
#include "QcCircuitFixture.h"
#include "QcTestUtil.h"

#include <QTemporaryDir>

#include <cmath>

using namespace Qt::StringLiterals;

// The backpack as a block (decision E-832): crafted from a chest in leather, placed with the contents
// its item carries, broken (by hand, in creative mode, by a command) into one item that carries them,
// kept through saving and loading as a block and as an item on the ground, never sent whole to
// clients that only see it lying there, and filled and emptied by hoppers that never put a backpack
// into it.
class TestBackpack : public QcCircuitFixture
{
    Q_OBJECT

    QcServerClient &player() { return *m_server->entities().clients().front(); }

    QcItemStack stackOf(const char *name, int count) const { return {item(name), count, 0}; }
    QcItemStack backpack() const { return stackOf("qtcraft:backpack", 1); }
    QcItemStack packed(QList<QcItemStack> contents) const { return backpack().withContents(std::move(contents)); }

    // Where the player places a block: two east of the feet, on the stage.
    QcBlockPos spot() const { return m_feet.offset(2, 0, 0); }

    // Puts `stack` into hotbar slot 0, selects it and places it on the stage at spot().
    void placeHeld(const QcItemStack &stack)
    {
        player().inventory.setSlot(0, stack);
        send(QcMsg::SelectHotbarSlot{0});
        send(QcMsg::UseItem{spot().offset(0, -1, 0), QcBlockFace::Up});
        QCOMPARE(m_content->block(*m_server->world().blockAt(spot())).name, u"qtcraft:backpack"_s);
    }

    void mine(QcBlockPos pos)
    {
        const QcBlockDefinition &block = m_content->block(*m_server->world().blockAt(pos));
        send(QcMsg::StartMining{pos});
        ticks(static_cast<int>(std::ceil(QcMining::breakSeconds(block, nullptr) * QcPhysics::tickRate)) + 1);
        send(QcMsg::FinishMining{pos});
        QCOMPARE(*m_server->world().blockAt(pos), qcAirBlockId);
    }

    // How many items on the ground are `wanted` (count included).
    int onTheGround(const QcItemStack &wanted) const
    {
        int found = 0;
        for (const QcItemEntity &dropped : m_server->items().items()) {
            found += dropped.stack == wanted ? 1 : 0;
        }
        return found;
    }

    // How many stacks of the player's inventory or on the ground are `wanted`.
    int anywhere(const QcItemStack &wanted)
    {
        int found = onTheGround(wanted);
        for (const QcItemStack &stack : player().inventory.allSlots()) {
            found += stack == wanted ? 1 : 0;
        }
        return found;
    }

    int itemsOnTheGround() const { return static_cast<int>(m_server->items().items().size()); }

    QList<QcItemStack> padded(QList<QcItemStack> stacks) const
    {
        stacks.resize(9);
        return stacks;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void itemAndRecipe()
    {
        const QcItemDefinition &definition = m_content->item(item("qtcraft:backpack"));
        QVERIFY(definition.portableContainer);
        QCOMPARE(definition.maxStack, 1);
        const QcBlockDefinition &block = m_content->block(id("qtcraft:backpack"));
        QVERIFY(block.portableContainer);
        QCOMPARE(block.containerSlots, 9);

        // A chest surrounded by eight leather.
        QList<QcItemStack> grid(9, stackOf("qtcraft:leather", 1));
        grid[4] = stackOf("qtcraft:chest", 1);
        const std::optional<qsizetype> recipe = QcCrafting::match(*m_content, grid, 3);
        QVERIFY(recipe);
        QCOMPARE(m_content->recipes()[*recipe].resultItem, u"qtcraft:backpack"_s);
    }

    void placingTakesTheContents()
    {
        join();
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 3)};
        placeHeld(packed(inside));
        QCOMPARE(m_server->blockEntities().container(spot())->stacks, padded(inside));
        // The stack is used up like any block.
        QVERIFY(player().inventory.slot(0).isEmpty());
        QCOMPARE(itemsOnTheGround(), 0);
    }

    void breakingKeepsTheContents()
    {
        join();
        placeHeld(backpack());
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), stackOf("qtcraft:cobblestone", 64)};
        m_server->blockEntities().container(spot())->stacks = padded(inside);
        mine(spot());
        ticks(1);

        // One backpack carrying everything, no loose items and no second, empty backpack.
        QCOMPARE(anywhere(packed(inside)), 1);
        QCOMPARE(anywhere(backpack()), 0);
        QCOMPARE(droppedCount(item("qtcraft:dirt")), 0);
        QCOMPARE(droppedCount(item("qtcraft:cobblestone")), 0);
    }

    void anEmptyBackpackDropsAsUsual()
    {
        join();
        placeHeld(backpack());
        mine(spot());
        ticks(1);
        QCOMPARE(anywhere(backpack()), 1);
        QCOMPARE(itemsOnTheGround() + int(std::ranges::count_if(player().inventory.allSlots(), [](const QcItemStack &s) { return !s.isEmpty(); })), 1);
    }

    void creativeBreakingDropsOnlyAFilledOne()
    {
        QcServerConfig config;
        config.defaultGameMode = QcGameMode::Creative;
        join(config);
        placeHeld(backpack());
        send(QcMsg::StartMining{spot()});
        send(QcMsg::FinishMining{spot()});
        ticks(1);
        QCOMPARE(itemsOnTheGround(), 0);

        placeHeld(backpack());
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 2)};
        m_server->blockEntities().container(spot())->stacks = padded(inside);
        send(QcMsg::StartMining{spot()});
        send(QcMsg::FinishMining{spot()});
        ticks(1);
        QCOMPARE(onTheGround(packed(inside)), 1);
        QCOMPARE(itemsOnTheGround(), 1);
    }

    void aCommandReplacingItKeepsTheContents()
    {
        join();
        placeHeld(backpack());
        const QList<QcItemStack> inside{{}, {}, stackOf("qtcraft:stick", 7)};
        m_server->blockEntities().container(spot())->stacks = padded(inside);
        chat(u"/setblock %1 %2 %3 stone"_s.arg(spot().x).arg(spot().y).arg(spot().z));
        ticks(1);
        QCOMPARE(anywhere(packed(inside)), 1);
        QCOMPARE(droppedCount(item("qtcraft:stick")), 0);
        // Another state of the same block (turning it) keeps the contents in place.
        put(1, 0, 1, u"backpack[facing=north]"_s);
        m_server->blockEntities().container(at(1, 0, 1))->stacks = padded(inside);
        put(1, 0, 1, u"backpack[facing=east]"_s);
        QCOMPARE(m_server->blockEntities().container(at(1, 0, 1))->stacks, padded(inside));
    }

    void contentsSurviveSavingAndLoading()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        config.autosaveSeconds = 0;
        buildArea(config);
        const QList<QcItemStack> inside{stackOf("qtcraft:dirt", 5), {}, stackOf("qtcraft:stick", 3).withName(u"Wand"_s)};
        put(2, 0, 2, u"backpack[facing=south]"_s);
        m_server->blockEntities().container(at(2, 0, 2))->stacks = padded(inside);
        const QcBlockPos ground = at(6, 0, 6);
        m_server->items().spawn({packed(inside), {ground.x + 0.5, static_cast<double>(ground.y), ground.z + 0.5}, {}}, m_server->entities().newId());
        player().inventory.setSlot(4, packed(inside));
        QVERIFY(m_server->save().has_value());
        const QcBlockPos origin = m_origin;
        cleanup();

        join(config);
        m_origin = origin;
        loadAround(at(2, 0, 2));
        loadAround(ground);
        QCOMPARE(m_server->blockEntities().container(at(2, 0, 2))->stacks, padded(inside));
        QCOMPARE(onTheGround(packed(inside)), 1);
        QCOMPARE(player().inventory.slot(4), packed(inside));
        QCOMPARE(m_client->stacks.value(4), packed(inside));
    }

    void clientsSeeADroppedBackpackWithoutItsContents()
    {
        join();
        const QcItemStack full = packed({stackOf("qtcraft:dirt", 5)}).withName(u"Kit"_s);
        const QcBlockPos ground = m_feet.offset(3, 0, 3);
        const quint32 entity = m_server->entities().newId();
        m_server->items().spawn({full, {ground.x + 0.5, static_cast<double>(ground.y), ground.z + 0.5}, {}, 1000}, entity);
        ticks(2);
        QVERIFY(m_client->items.contains(entity));
        const QcItemStack seen = m_client->items.value(entity);
        QCOMPARE(seen.item, full.item);
        QCOMPARE(seen.count, 1);
        QCOMPARE(seen.name(), u"Kit"_s);
        QVERIFY(!seen.hasContents());
        // The server keeps everything.
        QCOMPARE(onTheGround(full), 1);
    }

    void pickingItUpKeepsTheContents()
    {
        join();
        const QcItemStack full = packed({stackOf("qtcraft:dirt", 5), stackOf("qtcraft:stick", 2)});
        m_server->items().spawn({full, m_client->motion.position, {}}, m_server->entities().newId());
        idle(30);
        QCOMPARE(itemsOnTheGround(), 0);
        QCOMPARE(slotOf(item("qtcraft:backpack")) >= 0, true);
        QCOMPARE(player().inventory.slot(slotOf(item("qtcraft:backpack"))), full);
    }

    void hoppersFillAndEmptyButNeverInsertABackpack()
    {
        buildArea();
        // Hopper above pours dirt in, hopper below takes it out again.
        put(2, 2, 2, u"signal_hopper[facing=down]"_s);
        put(2, 1, 2, u"backpack[facing=south]"_s);
        m_server->blockEntities().container(at(2, 2, 2))->stacks[0] = stackOf("qtcraft:dirt", 1);
        ticks(10);
        QCOMPARE(m_server->blockEntities().container(at(2, 1, 2))->stacks.at(0), stackOf("qtcraft:dirt", 1));
        put(2, 0, 2, u"signal_hopper[facing=down]"_s);
        ticks(20);
        QVERIFY(m_server->blockEntities().container(at(2, 1, 2))->stacks.at(0).isEmpty());
        QCOMPARE(m_server->blockEntities().container(at(2, 0, 2))->stacks.at(0), stackOf("qtcraft:dirt", 1));

        // A backpack in the hopper above stays there; next to it, dirt still goes in.
        m_server->blockEntities().container(at(2, 0, 2))->stacks = QList<QcItemStack>(5);
        put(2, 0, 2, u"stone"_s);
        // A new hopper: an idle one rests until something changes around it.
        put(2, 2, 2, u"stone"_s);
        put(2, 2, 2, u"signal_hopper[facing=down]"_s);
        m_server->blockEntities().container(at(2, 2, 2))->stacks[0] = packed({stackOf("qtcraft:dirt", 1)});
        m_server->blockEntities().container(at(2, 2, 2))->stacks[1] = backpack();
        m_server->blockEntities().container(at(2, 2, 2))->stacks[2] = stackOf("qtcraft:stick", 1);
        ticks(40);
        const QList<QcItemStack> &above = m_server->blockEntities().container(at(2, 2, 2))->stacks;
        QCOMPARE(above.at(0), packed({stackOf("qtcraft:dirt", 1)}));
        QCOMPARE(above.at(1), backpack());
        QVERIFY(above.at(2).isEmpty());
        QCOMPARE(m_server->blockEntities().container(at(2, 1, 2))->stacks.at(0), stackOf("qtcraft:stick", 1));
        QVERIFY(QcPortableContainers::mayHoldAll(*m_content, m_server->blockEntities().container(at(2, 1, 2))->stacks));

        // A backpack goes into a chest by hopper like any item.
        put(2, 1, 2, u"chest[facing=south]"_s);
        ticks(40);
        QCOMPARE(m_server->blockEntities().container(at(2, 1, 2))->stacks.count(backpack()), 1);
    }
};

QTEST_GUILESS_MAIN(TestBackpack)
#include "test_backpack.moc"

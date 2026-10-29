// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcAnvil.h"
#include "gameplay/QcEnchantingFixture.h"
#include "QcTestUtil.h"
#include "server/mobs/QcMobManager.h"

#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

// The anvil in the game (decision E-866): the screen through the protocol (paying levels, refusing
// an output or cost the client did not see, the name field following the left stack, creative mode,
// quick moves, giving the slots back on disconnect, death and a mode switch), wear, falling anvils
// (hurting players and creatures, wearing on landing, dropping as an item onto a body), and name
// tags naming creatures (kept, and their death told to all players).
class TestAnvilScreen : public QcEnchantingFixture
{
    Q_OBJECT

private:
    static constexpr int leftSlot = QcMsg::MoveInventoryItem::machineInputSlot;
    static constexpr int rightSlot = QcMsg::MoveInventoryItem::machineFuelSlot;

    QcItemStack worn(const char *name, int damage) const
    {
        QcItemStack stack = stackOf(name);
        stack.damage = damage;
        return stack;
    }

    const QcMsg::AnvilState &anvilState() const { return m_client->workstations.anvilState; }

    // An anvil three blocks east of the player, opened through the protocol.
    QcBlockPos openAnvil(const char *block = "qtcraft:anvil")
    {
        const QcBlockPos anvil = tablePos();
        putAt(anvil.offset(0, -1, 0), id("qtcraft:stone"));
        putAt(anvil, id(block));
        send(QcMsg::UseItem{anvil, QcBlockFace::West});
        ticks(1);
        return anvil;
    }

    // Puts `stack` into the player's inventory and from there into the anvil's `slot`.
    void putIntoAnvil(const QcItemStack &stack, int slot)
    {
        player().inventory.setSlot(20, stack);
        move(20, slot);
    }

    // Asks for the output the client last saw, at `levelCost`.
    void forge(int levelCost) { send(QcMsg::ForgeAnvil{levelCost, anvilState().output}); }

    // A column of air `height` blocks above `pos`.
    void clearAbove(QcBlockPos pos, int height)
    {
        for (int dy = 0; dy <= height; ++dy) {
            putAt(pos.offset(0, dy, 0), qcAirBlockId);
        }
    }

    // Drops `block` from `height` blocks above `pos` and lets it land (manual ticks, no wall clock).
    void dropOnto(QcBlockPos pos, int height, const char *block = "qtcraft:anvil")
    {
        clearAbove(pos, height + 1);
        m_server->fallingBlocks().start(pos.offset(0, height, 0), id(block), m_server->entities().newId());
        for (int i = 0; i < 200 && !m_server->fallingBlocks().all().isEmpty(); ++i) {
            idle(1);
        }
        QVERIFY(m_server->fallingBlocks().all().isEmpty());
    }

    // A creature of `type` without a mind standing at `pos`.
    quint32 spawnStill(const QString &type, QcBlockPos pos)
    {
        const quint32 mob = m_server->mobs().spawn(*m_content->mobIndex(type), {pos.x + 0.5, static_cast<double>(pos.y), pos.z + 0.5},
                                                   m_server->entities().newId());
        m_server->mobs().find(mob)->noAi = true;
        return mob;
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void forgeTakesLevelsAndRefusesUnseenCosts()
    {
        join();
        player().experience = QcExperience(10, 0);
        const QcBlockPos anvil = openAnvil("qtcraft:damaged_anvil");
        QCOMPARE(m_client->workstations.anvil, std::optional(anvil));
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 200), leftSlot);
        putIntoAnvil({item("qtcraft:iron_ingot"), 2, 0}, rightSlot);
        send(QcMsg::RenameAnvil{u"Digger"_s});
        QCOMPARE(anvilState().name, u"Digger"_s);
        QCOMPARE(anvilState().levelCost, 3);
        QCOMPARE(anvilState().output.name(), u"Digger"_s);

        // A cost or an output the client did not see takes nothing.
        forge(2);
        send(QcMsg::ForgeAnvil{3, anvilState().output.withName(u"Other"_s)});
        QCOMPARE(player().experience.level(), 10);
        QCOMPARE(anvilState().left.damage, 200);

        forge(3);
        QCOMPARE(player().experience.level(), 7);
        QCOMPARE(anvilState().left.damage, 76);
        QCOMPARE(anvilState().left.name(), u"Digger"_s);
        QCOMPARE(anvilState().left.priorWork(), 1);
        QVERIFY(anvilState().right.isEmpty());

        // Closing gives the repaired pickaxe back.
        send(QcMsg::CloseAnvil{});
        const QcItemStack back = player().inventory.slot(serverSlotOf(item("qtcraft:iron_pickaxe")));
        QCOMPARE(back.name(), u"Digger"_s);
        QCOMPARE(back.damage, 76);
    }

    void namedItemsKeepTheirNameOnRepair()
    {
        join();
        player().experience = QcExperience(10, 0);
        openAnvil();
        // The name field takes the left stack's name: a repair alone costs the material only.
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 200).withName(u"Digger"_s), leftSlot);
        QCOMPARE(anvilState().name, u"Digger"_s);
        putIntoAnvil({item("qtcraft:iron_ingot"), 2, 0}, rightSlot);
        QCOMPARE(anvilState().output.name(), u"Digger"_s);
        QCOMPARE(anvilState().levelCost, 2);
        forge(2);
        QCOMPARE(anvilState().left.name(), u"Digger"_s);
        QCOMPARE(player().experience.level(), 8);

        // Another stack in the left slot brings its own name (none) into the field.
        move(leftSlot, 25);
        QVERIFY(anvilState().left.isEmpty());
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 200), leftSlot);
        QCOMPARE(anvilState().name, QString());
        putIntoAnvil({item("qtcraft:iron_ingot"), 1, 0}, rightSlot);
        QCOMPARE(anvilState().output.name(), QString());
        QCOMPARE(anvilState().levelCost, 1);
    }

    void notEnoughLevelsTakesNothing()
    {
        join();
        player().experience = QcExperience(1, 0);
        openAnvil();
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 200), leftSlot);
        putIntoAnvil({item("qtcraft:iron_ingot"), 2, 0}, rightSlot);
        forge(2);
        QCOMPARE(player().experience.level(), 1);
        QCOMPARE(anvilState().left.damage, 200);
        QCOMPARE(anvilState().right.count, 2);
    }

    void creativeIsFreeNeverRefusedAndNeverWears()
    {
        join();
        chat(u"/gamemode creative"_s);
        ticks(1);
        QCOMPARE(player().experience.level(), 0);
        const QcBlockPos anvil = openAnvil();
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 200).withPriorWork(6), leftSlot);
        putIntoAnvil({item("qtcraft:iron_ingot"), 1, 0}, rightSlot);
        QVERIFY(anvilState().levelCost >= QcAnvil::tooExpensive);
        forge(anvilState().levelCost);
        QCOMPARE(anvilState().left.damage, 138);
        QCOMPARE(player().experience.level(), 0);

        // Twenty renames would wear a survival anvil with a chance of 92 %.
        for (int i = 0; i < 20; ++i) {
            send(QcMsg::RenameAnvil{u"Name %1"_s.arg(i)});
            forge(anvilState().levelCost);
            QCOMPARE(anvilState().left.name(), u"Name %1"_s.arg(i));
        }
        QCOMPARE(blockNameAt(anvil), u"qtcraft:anvil"_s);
    }

    void quickMoveFillsLeftThenRight()
    {
        join();
        openAnvil();
        player().inventory.setSlot(20, worn("qtcraft:iron_pickaxe", 10));
        player().inventory.setSlot(21, worn("qtcraft:iron_pickaxe", 20));
        send(QcMsg::QuickMoveItem{20});
        QCOMPARE(anvilState().left.damage, 10);
        send(QcMsg::QuickMoveItem{21});
        QCOMPARE(anvilState().right.damage, 20);
    }

    void slotsComeBackOnDisconnectDeathAndModeSwitch()
    {
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.keepInventory = false;
        join(config);
        openAnvil();
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 10), leftSlot);
        m_client->connection.close();
        QcTestUtil::pump();
        ticks(1);
        rejoin();
        QCOMPARE(countOf(item("qtcraft:iron_pickaxe")), 1);

        // A switch to spectator gives the slots back.
        openAnvil();
        move(slotOf(item("qtcraft:iron_pickaxe")), leftSlot);
        chat(u"/gamemode spectator"_s);
        ticks(1);
        QVERIFY(!player().screen.anvil.has_value());
        chat(u"/gamemode survival"_s);
        ticks(1);
        QCOMPARE(countOf(item("qtcraft:iron_pickaxe")), 1);
        QCOMPARE(droppedCount(item("qtcraft:iron_pickaxe")), 0);

        // Death drops them.
        openAnvil();
        move(slotOf(item("qtcraft:iron_pickaxe")), leftSlot);
        chat(u"/kill"_s);
        ticks(2);
        QVERIFY(!player().screen.anvil.has_value());
        QCOMPARE(droppedCount(item("qtcraft:iron_pickaxe")), 1);
    }

    void anvilsWearAndBreak()
    {
        // Each step wears into the next; the damaged anvil breaks.
        QCOMPARE(m_content->block(id("qtcraft:anvil")).wearsInto, u"qtcraft:chipped_anvil"_s);
        QCOMPARE(m_content->block(id("qtcraft:chipped_anvil")).wearsInto, u"qtcraft:damaged_anvil"_s);
        QVERIFY(m_content->block(id("qtcraft:damaged_anvil")).wearsInto.isEmpty());
        QVERIFY(m_content->block(id("qtcraft:damaged_anvil")).opensAnvil);

        // The screen of a broken anvil closes and gives the slots back.
        join();
        const QcBlockPos anvil = openAnvil();
        putIntoAnvil(worn("qtcraft:iron_pickaxe", 10), leftSlot);
        QVERIFY(m_server->world().setBlock(anvil, qcAirBlockId));
        ticks(2);
        QVERIFY(!m_client->workstations.anvil);
        QVERIFY(serverSlotOf(item("qtcraft:iron_pickaxe")) >= 0);
    }

    void fallingAnvilHurtsThePlayerBelow()
    {
        join();
        const double before = m_client->health;
        dropOnto(m_feet, 8);
        // Seven blocks beyond the first: 2 per block.
        QCOMPARE(before - m_client->health, 14.0);
    }

    void fallingAnvilWearsAndDropsOntoABody()
    {
        join();
        // From 25 blocks the chance to wear is above one: it lands chipped.
        const QcBlockPos open = m_feet.offset(-2, 0, -2);
        dropOnto(open, 25);
        QCOMPARE(blockNameAt(open), u"qtcraft:chipped_anvil"_s);

        // An iron golem in the cell: hurt by 2 × 2, and the anvil drops as an item.
        const QcBlockPos cell = m_feet.offset(2, 0, -2);
        const quint32 golem = spawnStill(u"qtcraft:iron_golem"_s, cell);
        const double health = m_server->mobs().find(golem)->health;
        dropOnto(cell, 3);
        QCOMPARE(health - m_server->mobs().find(golem)->health, 4.0);
        QVERIFY(blockNameAt(cell) != u"qtcraft:anvil"_s);
        QCOMPARE(droppedCount(item("qtcraft:anvil")) + droppedCount(item("qtcraft:chipped_anvil")), 1);
    }

    void nameTagsNameCreatures()
    {
        join();
        const QcBlockPos at = m_feet.offset(2, 0, 0);
        const quint32 sheep = m_server->mobs().spawn(*m_content->mobIndex(u"qtcraft:sheep"_s), {at.x + 0.5, static_cast<double>(at.y), at.z + 0.5},
                                                     m_server->entities().newId());
        // An unnamed tag does nothing.
        hold(u"name_tag"_s);
        send(QcMsg::UseEntity{sheep});
        QCOMPARE(m_server->mobs().find(sheep)->name, QString());

        const int slot = player().inventory.selectedHotbarSlot();
        player().inventory.setSlot(slot, QcItemStack{item("qtcraft:name_tag"), 1, 0}.withName(u"Dolly"_s));
        send(QcMsg::UseEntity{sheep});
        QcCreature *named = m_server->mobs().find(sheep);
        QCOMPARE(named->name, u"Dolly"_s);
        QVERIFY(named->persistent);
        QVERIFY(player().inventory.slot(slot).isEmpty());
        // Kept with the world under its name.
        QCOMPARE(QcMobManager::storedOf(*named).name, u"Dolly"_s);
        // Letting it go (a family releasing it, a carried block put down) keeps it.
        m_server->mobs().keep(*named, false);
        QVERIFY(named->persistent);
        m_server->mobs().release(*named);
        QVERIFY(named->persistent);
    }

    void namedDeathsAreToldToAllPlayers()
    {
        join();
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd));
        RawClient other(std::move(clientEnd));
        other.hello(u"Other"_s);
        QcTestUtil::pump();
        ticks(2);

        const QcBlockPos cell = m_feet.offset(2, 0, 2);
        const quint32 sheep = spawnStill(u"qtcraft:sheep"_s, cell);
        m_server->mobs().find(sheep)->name = u"Dolly"_s;
        dropOnto(cell, 20);
        QcTestUtil::pump();
        QVERIFY(!m_server->mobs().find(sheep));
        for (const RawClient *client : {static_cast<const RawClient *>(m_client.get()), static_cast<const RawClient *>(&other)}) {
            const bool told = std::ranges::any_of(client->chatKeys, [](const QcMsg::ChatMessage &line) {
                return line.key == u"%1 died"_s && line.arguments == QStringList{u"Dolly"_s};
            });
            QVERIFY(told);
        }
    }
};

QTEST_GUILESS_MAIN(TestAnvilScreen)
#include "test_anvil_screen.moc"

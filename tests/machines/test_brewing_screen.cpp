// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientMachineScreen.h"
#include "common/protocol/QcProtocol.h"
#include "fixtures/QcPlayerUseFixture.h"
#include "fixtures/QcTestUtil.h"
#include "server/machines/QcBrewingRules.h"

using namespace Qt::StringLiterals;

// The stand's screen over the protocol (decision E-1063), the cauldron (E-1065) and what potions
// do when drunk (E-1061): all through the messages a client sends.
class TestBrewingScreen : public QcPlayerUseFixture
{
    Q_OBJECT

private:
    static constexpr int firstBottle = QcMsg::MoveInventoryItem::machineOutputSlot;

    QcBlockPos openStand()
    {
        const QcBlockPos stand = m_feet.offset(2, 0, 0);
        putAt(stand, id("qtcraft:brewing_stand"));
        [[maybe_unused]] const bool made = ensureEntity(stand);
        send(QcMsg::UseItem{stand, QcBlockFace::West});
        ticks(1);
        return stand;
    }

    const QcBlockEntities::Machine &standAt(QcBlockPos pos) const { return *m_server->blockEntities().machine(pos); }

    // A cauldron next to the player at `level`.
    QcBlockPos cauldron(int level)
    {
        const QcBlockPos pos = m_feet.offset(2, 0, 0);
        putAt(pos, m_content->withProperty(id("qtcraft:cauldron"), u"level"_s, QString::number(level)));
        return pos;
    }

    QString level(QcBlockPos pos) const { return m_content->property(*m_server->world().blockAt(pos), u"level"_s); }

    void use(QcBlockPos pos) { send(QcMsg::UseItem{pos, QcBlockFace::West}); }

private slots:
    void init()
    {
        join();
        chat(u"/gamemode survival"_s);
    }

    void theScreenOpensWithFiveSlots()
    {
        const QcBlockPos stand = openStand();
        QCOMPARE(m_client->openMachine, std::optional(stand));
        QCOMPARE(m_client->machineCategory, QString::fromLatin1(QcClientMachineScreen::brewingCategory));
        QCOMPARE(m_client->machineStacks.size(), QcBrewerDefinition::slotCount);
    }

    void slotsTakeOnlyWhatFits()
    {
        chat(u"/give dirt 1"_s);
        chat(u"/give sugar 2"_s);
        chat(u"/give ember_dust 1"_s);
        chat(u"/give water_bottle 1"_s);
        const QcBlockPos stand = openStand();

        move(slotOf(baseItem(u"dirt"_s)), inputSlot);
        move(slotOf(baseItem(u"dirt"_s)), firstBottle);
        move(slotOf(baseItem(u"dirt"_s)), fuelSlot);
        move(slotOf(baseItem(u"sugar"_s)), fuelSlot);
        move(slotOf(baseItem(u"sugar"_s)), firstBottle);
        move(slotOf(baseItem(u"water_bottle"_s)), inputSlot);
        for (const QcItemStack &slot : standAt(stand).stacks) {
            QVERIFY(slot.isEmpty());
        }

        move(slotOf(baseItem(u"sugar"_s)), inputSlot, 2);
        move(slotOf(baseItem(u"ember_dust"_s)), fuelSlot);
        move(slotOf(baseItem(u"water_bottle"_s)), firstBottle + 2);
        ticks(1);
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::ingredientSlot].count, 2);
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::firstBottleSlot + 2].item, baseItem(u"water_bottle"_s));
        QCOMPARE(m_client->machineStacks.value(QcBrewingRules::firstBottleSlot + 2).item, baseItem(u"water_bottle"_s));
    }

    void bottlesComeBackOutAndABottleSlotHoldsOne()
    {
        chat(u"/give awkward_potion 2"_s);
        const QcBlockPos stand = openStand();
        const int first = slotOf(baseItem(u"awkward_potion"_s));
        move(first, firstBottle);
        const int second = slotOf(baseItem(u"awkward_potion"_s));
        move(second, firstBottle);
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::firstBottleSlot].count, 1);
        QCOMPARE(countOf(baseItem(u"awkward_potion"_s)), 1);

        move(firstBottle, first);
        QCOMPARE(countOf(baseItem(u"awkward_potion"_s)), 2);
        QVERIFY(standAt(stand).stacks[QcBrewingRules::firstBottleSlot].isEmpty());
    }

    void whatLayInASlotMayStay()
    {
        // Dirt a mod's change left in the ingredient slot: it blocks no other slot, and comes out.
        chat(u"/give ember_dust 2"_s);
        const QcBlockPos stand = openStand();
        m_server->blockEntities().machine(stand)->stacks[QcBrewingRules::ingredientSlot] = QcItemStack{baseItem(u"dirt"_s), 2, 0};
        ticks(1);
        move(slotOf(baseItem(u"ember_dust"_s)), fuelSlot);
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::fuelSlot].item, baseItem(u"ember_dust"_s));
        move(inputSlot, 8);
        QVERIFY(standAt(stand).stacks[QcBrewingRules::ingredientSlot].isEmpty());
        QCOMPARE(countOf(baseItem(u"dirt"_s)), 2);
    }

    void quickMoveFindsTheRightSlot()
    {
        chat(u"/give ember_dust 3"_s);
        chat(u"/give sugar 1"_s);
        chat(u"/give awkward_potion 1"_s);
        const QcBlockPos stand = openStand();
        send(QcMsg::QuickMoveItem{slotOf(baseItem(u"ember_dust"_s))});
        send(QcMsg::QuickMoveItem{slotOf(baseItem(u"sugar"_s))});
        send(QcMsg::QuickMoveItem{slotOf(baseItem(u"awkward_potion"_s))});
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::fuelSlot].item, baseItem(u"ember_dust"_s));
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::ingredientSlot].item, baseItem(u"sugar"_s));
        QCOMPARE(standAt(stand).stacks[QcBrewingRules::firstBottleSlot].item, baseItem(u"awkward_potion"_s));
    }

    void drinkingGivesTheEffectsAndLeavesTheBottle()
    {
        hold(u"speed_potion"_s);
        send(QcMsg::ConsumeItem{});
        const QcStatusEffect *speed = player().effects.find(u"qtcraft:speed"_s);
        QVERIFY(speed);
        QVERIFY(speed->ticksLeft > 3500);
        QCOMPARE(countOf(baseItem(u"speed_potion"_s)), 0);
        QCOMPARE(countOf(baseItem(u"glass_bottle"_s)), 1);
    }

    void aHealingPotionHealsAtOnce()
    {
        hold(u"healing_potion"_s);
        player().health = 10.0;
        send(QcMsg::ConsumeItem{});
        ticks(1);
        QCOMPARE(player().health, 14.0);
    }

    void aGlassBottleTakesOneLevel()
    {
        hold(u"glass_bottle"_s, 4);
        const QcBlockPos pot = cauldron(3);
        use(pot);
        QCOMPARE(level(pot), u"2"_s);
        QCOMPARE(countOf(baseItem(u"water_bottle"_s)), 1);
        send(QcMsg::SelectHotbarSlot{slotOf(baseItem(u"glass_bottle"_s))});
        use(pot);
        send(QcMsg::SelectHotbarSlot{slotOf(baseItem(u"glass_bottle"_s))});
        use(pot);
        QCOMPARE(level(pot), u"0"_s);

        send(QcMsg::SelectHotbarSlot{slotOf(baseItem(u"glass_bottle"_s))});
        use(pot);
        QCOMPARE(countOf(baseItem(u"water_bottle"_s)), 3);
        QCOMPARE(countOf(baseItem(u"glass_bottle"_s)), 1);
    }

    void aWaterBottleGivesALevelBack()
    {
        hold(u"water_bottle"_s);
        const QcBlockPos pot = cauldron(1);
        use(pot);
        QCOMPARE(level(pot), u"2"_s);
        QCOMPARE(countOf(baseItem(u"glass_bottle"_s)), 1);
    }

    void aFullCauldronLeavesTheBottleToBeDrunk()
    {
        hold(u"water_bottle"_s);
        const QcBlockPos pot = cauldron(3);
        use(pot);
        QCOMPARE(level(pot), u"3"_s);
        QCOMPARE(countOf(baseItem(u"water_bottle"_s)), 0);
        QCOMPARE(countOf(baseItem(u"glass_bottle"_s)), 1);
    }

    void aFullCauldronStaysFullUnderAWaterBucket()
    {
        hold(u"water_bucket"_s);
        const QcBlockPos pot = cauldron(3);
        const QcBlockId beside = *m_server->world().blockAt(pot.offset(-1, 0, 0));
        use(pot);
        QCOMPARE(level(pot), u"3"_s);
        QCOMPARE(countOf(baseItem(u"bucket"_s)), 1);
        QCOMPARE(*m_server->world().blockAt(pot.offset(-1, 0, 0)), beside);
    }

    void bucketsFillAndEmptyTheWholeCauldron()
    {
        hold(u"water_bucket"_s);
        const QcBlockPos pot = cauldron(0);
        use(pot);
        QCOMPARE(level(pot), u"3"_s);
        QCOMPARE(countOf(baseItem(u"bucket"_s)), 1);
        use(pot);
        QCOMPARE(level(pot), u"0"_s);
        QCOMPARE(countOf(baseItem(u"water_bucket"_s)), 1);
    }
};

QTEST_GUILESS_MAIN(TestBrewingScreen)
#include "test_brewing_screen.moc"

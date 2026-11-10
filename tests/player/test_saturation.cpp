// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcServerTestFixture.h"
#include "server/gameplay/QcHunger.h"
#include "server/gameplay/QcPlayerHealth.h"

using namespace Qt::StringLiterals;

// The hidden saturation in the server (decision E-1001): the values of the base food, eating,
// the saturation effect and respawning, through the real paths. The rules of QcHunger itself
// stand in test_hunger. Ticks are stepped by hand.
class TestSaturation : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcServerClient &player() { return *m_server->entities().clients().front(); }

    // A survival player with `food` points and no saturation.
    void stage(int food)
    {
        join();
        chat(u"/gamerule mobSpawning false"_s);
        chat(u"/gamemode survival"_s);
        ticks(1);
        player().hunger.restore(food, 0.0, 0.0);
        idle(2);
    }

    // Hurts the player by `amount` through the real damage path (which restarts the regeneration).
    void hurt(double amount)
    {
        m_server->systems().health.damage(player(), amount, {}, QcPlayerHealth::DamageKind::Unblockable);
    }

    // A survival player with a full bar, `saturation` and `health`, his regeneration timer at 0;
    // without invulnerability, so that every later hit counts.
    void stageHurt(double health, double saturation)
    {
        stage(QcHunger::maxFood);
        chat(u"/gamerule playerInvulnerabilityTicks 0"_s);
        hurt(player().health - health);
        QCOMPARE(player().health, health);
        player().hunger.restore(QcHunger::maxFood, 0.0, saturation);
    }

    // The saturation modifier of the base item `name`.
    double modifierOf(const char *name) const { return m_content->item(item(name)).saturation; }

private slots:
    void baseFoodHasTheReferenceSaturation()
    {
        QCOMPARE(modifierOf("qtcraft:bread"), 0.6);
        QCOMPARE(modifierOf("qtcraft:apple"), 0.3);
        QCOMPARE(modifierOf("qtcraft:cooked_beef"), 0.8);
        QCOMPARE(modifierOf("qtcraft:cooked_salmon"), 0.8);
        QCOMPARE(modifierOf("qtcraft:raw_cod"), 0.1);
        QCOMPARE(modifierOf("qtcraft:rotten_flesh"), 0.1);
        QCOMPARE(modifierOf("qtcraft:pumpkin_pie"), 0.3);
        QCOMPARE(m_content->block(id("qtcraft:cake")).bites.saturation, 0.1);

        // Every food of the base game names its modifier within the reference's range.
        for (qsizetype id = 1; id <= m_content->itemCount(); ++id) {
            const QcItemDefinition &food = m_content->item(static_cast<QcItemId>(id));
            if (food.food > 0) {
                QVERIFY2(food.saturation >= 0.1 && food.saturation <= 1.2, qPrintable(food.name));
            }
        }
    }

    void eatingGivesFoodAndSaturation()
    {
        stage(10);
        chat(u"/give bread 1"_s);
        send(QcMsg::SelectHotbarSlot{slotOf(item("qtcraft:bread"))});
        send(QcMsg::ConsumeItem{});
        QCOMPARE(player().hunger.food(), 15);
        QCOMPARE(player().hunger.saturation(), 6.0);
        QCOMPARE(m_client->food, 15);
    }

    // Saturation gives one food point and two saturation per level every tick, even when full.
    void saturationEffectFillsFoodAndSaturation()
    {
        stage(10);
        chat(u"/effect give Raw saturation 1 1"_s);
        ticks(1);
        QcTestUtil::pump();
        QCOMPARE(player().hunger.food(), 11);
        QCOMPARE(player().hunger.saturation(), 2.0);
        idle(4);
        QCOMPARE(player().hunger.food(), 15);
        QCOMPARE(player().hunger.saturation(), 10.0);
        idle(30);
        QCOMPARE(player().hunger.food(), QcHunger::maxFood);
        QCOMPARE(player().hunger.saturation(), 20.0);
        QCOMPARE(m_client->food, QcHunger::maxFood);
    }

    // A full bar with saturation heals min(saturation, 6) / 6 every 0.5 s (10 ticks at 20 Hz),
    // paying from the saturation; the client gets the new health.
    void fullBarHealsQuicklyFromSaturation()
    {
        stageHurt(10.0, 6.0);
        idle(9);
        QCOMPARE(player().health, 10.0);
        idle(1);
        QCOMPARE(player().health, 11.0);
        QCOMPARE(m_client->health, 11.0);
        QCOMPARE(player().hunger.food(), QcHunger::maxFood);
        QCOMPARE(player().hunger.saturation(), 5.0);

        // The next step has only 5 saturation left: five sixths of a point.
        idle(10);
        QCOMPARE(player().health, 11.0 + 5.0 / 6.0);
        QCOMPARE(m_client->health, player().health);
        QVERIFY(player().hunger.saturation() < 5.0);
    }

    void hurtRestartsTheFastRegeneration()
    {
        stageHurt(10.0, 6.0);
        idle(8);
        hurt(1.0);
        QCOMPARE(player().health, 9.0);

        // Without the hurt the step would come after two more ticks; now it takes ten.
        idle(9);
        QCOMPARE(player().health, 9.0);
        QCOMPARE(m_client->health, 9.0);
        idle(1);
        QCOMPARE(player().health, 10.0);
        QCOMPARE(m_client->health, 10.0);
    }

    void respawnGivesAFullBarAndTheSpawnSaturation()
    {
        stage(3);
        chat(u"/kill"_s);
        send(QcMsg::Respawn{});
        ticks(1);
        QCOMPARE(player().hunger.food(), QcHunger::maxFood);
        QCOMPARE(player().hunger.saturation(), QcHunger::spawnSaturation);
        QCOMPARE(m_client->food, QcHunger::maxFood);
    }
};

QTEST_GUILESS_MAIN(TestSaturation)
#include "test_saturation.moc"

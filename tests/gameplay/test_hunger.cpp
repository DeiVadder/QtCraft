#include "QcHunger.h"

#include <QTest>

#include <cmath>

namespace {

constexpr double dt = 0.05;

// Ticks the hunger for `seconds` and applies the health changes; returns the new health.
double run(QcHunger &hunger, double health, double seconds)
{
    for (int i = 0; i < int(std::lround(seconds / dt)); ++i) {
        health += hunger.tick(dt, health, 20.0);
    }
    return health;
}

} // namespace

// Food points: exhaustion, eating, regeneration only when fed, starvation down to a floor.
class TestHunger : public QObject
{
    Q_OBJECT

private slots:
    void exhaustionCostsFoodPoints()
    {
        QcHunger hunger;
        QCOMPARE(hunger.food(), 20);
        hunger.exhaust(3.75);
        QCOMPARE(hunger.food(), 20);
        hunger.exhaust(0.5);
        QCOMPARE(hunger.food(), 19);
        QCOMPARE(hunger.exhaustion(), 0.25);

        // Eight points of exhaustion are two food points.
        hunger.exhaust(8.0);
        QCOMPARE(hunger.food(), 17);
        hunger.exhaust(1000.0);
        QCOMPARE(hunger.food(), 0);
        hunger.exhaust(-5.0);
        QCOMPARE(hunger.food(), 0);
    }

    void movementCostsDependOnTheActivity()
    {
        // 10 m walking 0.1, sprinting 1.0, swimming 0.15; a jump 0.05, a sprinting jump 0.2.
        QCOMPARE(QcHunger::movementExhaustion(10.0, false, false, false), 0.1);
        QCOMPARE(QcHunger::movementExhaustion(10.0, true, false, false), 1.0);
        QCOMPARE(QcHunger::movementExhaustion(10.0, false, true, false), 0.15);
        QCOMPARE(QcHunger::movementExhaustion(0.0, false, false, true), 0.05);
        QCOMPARE(QcHunger::movementExhaustion(0.0, true, false, true), 0.2);
        QCOMPARE(QcHunger::movementExhaustion(-3.0, false, false, false), 0.0);
        QCOMPARE(QcHunger::movementExhaustion(std::nan(""), false, false, false), 0.0);
    }

    void eatingFillsUpToTheMaximum()
    {
        QcHunger hunger;
        QVERIFY(!hunger.eat(3));
        hunger.restore(15, 0.0);
        QVERIFY(hunger.eat(3));
        QCOMPARE(hunger.food(), 18);
        QVERIFY(hunger.eat(8));
        QCOMPARE(hunger.food(), 20);
        QVERIFY(!hunger.eat(0));
    }

    void regenerationNeedsAlmostFullFood()
    {
        QcHunger hunger;
        hunger.restore(17, 0.0);
        QCOMPARE(run(hunger, 10.0, 20.0), 10.0);
        hunger.restore(18, 0.0);

        // One point every 4 s, each costing 3 exhaustion.
        QCOMPARE(run(hunger, 10.0, 4.0), 11.0);
        QCOMPARE(hunger.exhaustion(), 3.0);
        QCOMPARE(run(hunger, 11.0, 4.0), 12.0);
        QCOMPARE(hunger.food(), 17);

        // Now below the gate: no more healing.
        QCOMPARE(run(hunger, 12.0, 8.0), 12.0);
        // Damage restarts the timer.
        hunger.restore(20, 0.0);
        QCOMPARE(run(hunger, 12.0, 3.0), 12.0);
        hunger.hurt();
        QCOMPARE(run(hunger, 12.0, 3.0), 12.0);
        QCOMPARE(run(hunger, 12.0, 1.0), 13.0);

        // Full health needs no healing and costs nothing.
        hunger.restore(20, 0.0);
        QCOMPARE(run(hunger, 20.0, 8.0), 20.0);
        QCOMPARE(hunger.exhaustion(), 0.0);
    }

    void starvationStopsAtTheFloor()
    {
        QcHunger hunger;
        hunger.restore(0, 0.0);
        QCOMPARE(run(hunger, 5.0, 4.0), 4.0);
        QCOMPARE(run(hunger, 4.0, 3.5), 4.0);
        // From 4 to the floor of 1 takes 3 x 4 s and stays there.
        QCOMPARE(run(hunger, 4.0, 60.0), QcHunger::starvationFloor);
        hunger.restore(0, 0.0);
        QCOMPARE(run(hunger, 1.5, 8.0), 1.0);
        // One food point ends starving without healing.
        hunger.restore(1, 0.0);
        QCOMPARE(run(hunger, 5.0, 20.0), 5.0);
    }

    void restoredValuesAreClamped()
    {
        QcHunger hunger;
        hunger.restore(99, 77.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.exhaustion(), QcHunger::exhaustionPerFood);
        hunger.restore(-4, std::nan(""));
        QCOMPARE(hunger.food(), 0);
        QCOMPARE(hunger.exhaustion(), 0.0);
    }
};

QTEST_GUILESS_MAIN(TestHunger)
#include "test_hunger.moc"

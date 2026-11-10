// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "server/gameplay/QcHunger.h"

#include <QTest>

#include <cmath>

namespace {

constexpr double dt = 0.05;

// Ticks the hunger for `seconds` and applies the health changes; returns the new health.
double run(QcHunger &hunger, double health, double seconds)
{
    for (int i = 0; i < static_cast<int>(std::lround(seconds / dt)); ++i) {
        health += hunger.tick(dt, health, 20.0);
    }
    return health;
}

// Food left after `minutes` of steady movement at `speed`, with `jumpsPerSecond` jumps on the way.
int foodAfter(double minutes, double speed, bool sprinting, double jumpsPerSecond)
{
    QcHunger hunger;
    hunger.restore(QcHunger::maxFood, 0.0, 0.0);
    const int ticks = static_cast<int>(std::lround(minutes * 60.0 / dt));
    const int ticksPerJump = jumpsPerSecond > 0.0 ? static_cast<int>(std::lround(1.0 / (jumpsPerSecond * dt))) : 0;
    for (int i = 0; i < ticks; ++i) {
        const bool jumped = ticksPerJump > 0 && i % ticksPerJump == 0;
        hunger.exhaust(QcHunger::movementExhaustion(speed * dt, sprinting, false, jumped));
    }
    return hunger.food();
}

// What a sprint-and-mine routine costs: food eaten and when the first meal is due.
struct Routine
{
    int breadsEaten = 0;
    double firstMealSeconds = 0.0;
};

// Sprints for `minutes` like the server steps (one movement step per 20 Hz tick), with a sprinting
// jump and a mined block every two seconds, and eats bread (5 food points) whenever 5 points are
// missing. `saturated` plays with saturation (decision E-1001), else like before it: no
// saturation at the start and none from the bread.
Routine sprintAndMine(double minutes, bool saturated)
{
    QcHunger hunger;
    hunger.restore(QcHunger::maxFood, 0.0, saturated ? QcHunger::spawnSaturation : 0.0);
    const double modifier = saturated ? 0.6 : 0.0;
    const int ticks = static_cast<int>(std::lround(minutes * 60.0 / dt));
    const int ticksPerAction = static_cast<int>(std::lround(2.0 / dt));
    Routine routine;
    for (int i = 0; i < ticks; ++i) {
        const bool action = i % ticksPerAction == 0;
        hunger.exhaust(QcHunger::movementExhaustion(QcPhysics::sprintSpeed * dt, true, false, action));
        if (action) {
            hunger.exhaust(QcHunger::mineBlock);
        }
        if (hunger.food() <= QcHunger::maxFood - 5 && hunger.eat(5, modifier)) {
            routine.firstMealSeconds = routine.breadsEaten == 0 ? i * dt : routine.firstMealSeconds;
            ++routine.breadsEaten;
        }
    }
    return routine;
}

} // namespace

// Food points and saturation: exhaustion, eating, regeneration only when fed, starvation down to a
// floor.
class TestHunger : public QObject
{
    Q_OBJECT

private slots:
    void exhaustionCostsFoodPoints()
    {
        QcHunger hunger;
        hunger.restore(20, 0.0, 0.0);
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
        // 100 m walking 0.25, sprinting 3.0, swimming 0.5; a jump 0.02, a sprinting jump 0.08.
        QCOMPARE(QcHunger::movementExhaustion(100.0, false, false, false), 0.25);
        QCOMPARE(QcHunger::movementExhaustion(100.0, true, false, false), 3.0);
        QCOMPARE(QcHunger::movementExhaustion(100.0, false, true, false), 0.5);
        QCOMPARE(QcHunger::movementExhaustion(0.0, false, false, true), 0.02);
        QCOMPARE(QcHunger::movementExhaustion(0.0, true, false, true), 0.08);
        QCOMPARE(QcHunger::movementExhaustion(-3.0, false, false, false), 0.0);
        QCOMPARE(QcHunger::movementExhaustion(std::nan(""), false, false, false), 0.0);
    }

    // Pins the pace of hunger in play time (decision E-100), stepping like the server does: one
    // movement step per 20 Hz tick at the player's speed.
    void activitiesCostFoodAtAPlayablePace()
    {
        // Ten minutes of walking (2580 m) cost one food point; a full bar lasts about two hours.
        QCOMPARE(foodAfter(10.0, QcPhysics::walkSpeed, false, 0.0), 19);
        QCOMPARE(foodAfter(120.0, QcPhysics::walkSpeed, false, 0.0), 1);

        // Five minutes of sprinting (1680 m) cost 12 food points, sprint-jumping (one jump per
        // second) 18; the bar is empty after eight minutes of sprinting.
        QCOMPARE(foodAfter(5.0, QcPhysics::sprintSpeed, true, 0.0), 8);
        QCOMPARE(foodAfter(5.0, QcPhysics::sprintSpeed, true, 1.0), 2);
        QCOMPARE(foodAfter(8.0, QcPhysics::sprintSpeed, true, 0.0), 0);

        // Mining 200 blocks and 20 hits in a fight cost 3 exhaustion together, less than one food
        // point.
        QcHunger hunger;
        hunger.restore(20, 0.0, 0.0);
        for (int i = 0; i < 200; ++i) {
            hunger.exhaust(QcHunger::mineBlock);
        }
        for (int i = 0; i < 20; ++i) {
            hunger.exhaust(QcHunger::attack);
        }
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.exhaustion(), 3.0);

        QCOMPARE(std::round(QcHunger::minutesForFullBar(QcHunger::walkPerMetre, QcPhysics::walkSpeed)), 124.0);
        QCOMPARE(std::round(QcHunger::minutesForFullBar(QcHunger::sprintPerMetre, QcPhysics::sprintSpeed)), 8.0);
    }

    void eatingFillsUpToTheMaximum()
    {
        QcHunger hunger;
        QVERIFY(!hunger.eat(3, 0.6));
        hunger.restore(15, 0.0, 0.0);
        QVERIFY(hunger.eat(3, 0.6));
        QCOMPARE(hunger.food(), 18);
        QVERIFY(hunger.eat(8, 0.6));
        QCOMPARE(hunger.food(), 20);
        QVERIFY(!hunger.eat(0, 0.6));
    }

    // Food gives points × modifier × 2 saturation, never more than the food points (E-1001).
    void eatingGivesSaturationUpToTheFood()
    {
        QcHunger hunger;
        hunger.restore(10, 0.0, 0.0);
        QVERIFY(hunger.eat(5, 0.6));
        QCOMPARE(hunger.food(), 15);
        QCOMPARE(hunger.saturation(), 6.0);
        // Cooked beef: 8 points and 12.8 saturation, capped at the 20 food points.
        QVERIFY(hunger.eat(8, 0.8));
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 18.8);
        hunger.restore(1, 0.0, 0.0);
        QVERIFY(hunger.eat(2, 0.8));
        QCOMPARE(hunger.saturation(), 3.0);
        // A bad modifier gives no saturation, a full bar takes nothing.
        hunger.restore(10, 0.0, 0.0);
        QVERIFY(hunger.eat(2, std::nan("")));
        QCOMPARE(hunger.saturation(), 0.0);
        hunger.restore(20, 0.0, 0.0);
        QVERIFY(!hunger.eat(5, 0.6));
        QCOMPARE(hunger.saturation(), 0.0);

        // The saturation effect feeds even a full bar; frozen hunger takes nothing.
        hunger.feed(1, 2.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 2.0);
        hunger.restore(18, 0.0, 0.0);
        hunger.feed(3, 30.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 20.0);
        hunger.setFrozen(true);
        hunger.restore(5, 0.0, 0.0);
        hunger.feed(3, 2.0);
        QCOMPARE(hunger.food(), 5);
    }

    void exhaustionDrainsTheSaturationFirst()
    {
        QcHunger hunger;
        QCOMPARE(hunger.saturation(), QcHunger::spawnSaturation);
        hunger.restore(20, 0.0, 2.5);
        hunger.exhaust(4.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 1.5);
        // The last part of a point of saturation is gone with the next 4 exhaustion, then food.
        hunger.exhaust(8.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 0.0);
        hunger.exhaust(4.0);
        QCOMPARE(hunger.food(), 19);
        QCOMPARE(hunger.saturation(), 0.0);
        hunger.exhaust(1000.0);
        QCOMPARE(hunger.food(), 0);
    }

    // The scenario of 85.1c: with saturation a sprint-and-mine routine needs far fewer meals and
    // the first one comes later; prints the numbers.
    // A measurement of the QcHunger model, not golden numbers: the log shows the values (E-1001:
    // 18 breads, first after 95 s without saturation; 8 breads, first after 190 s with it), the
    // checks only the claim that saturation makes food last at least twice as long.
    void saturationMakesFoodLastLonger()
    {
        const Routine before = sprintAndMine(30.0, false);
        const Routine after = sprintAndMine(30.0, true);
        qInfo("30 min sprint and mine: %d breads, first after %.0f s without saturation; %d breads, "
              "first after %.0f s with it",
              before.breadsEaten, before.firstMealSeconds, after.breadsEaten, after.firstMealSeconds);
        QVERIFY(after.breadsEaten > 0);
        QVERIFY(after.breadsEaten <= before.breadsEaten / 2);
        QVERIFY(after.firstMealSeconds >= 2.0 * before.firstMealSeconds - 1.0);
    }

    void regenerationNeedsAlmostFullFood()
    {
        QcHunger hunger;
        hunger.restore(17, 0.0, 0.0);
        QCOMPARE(run(hunger, 10.0, 20.0), 10.0);
        hunger.restore(18, 0.0, 0.0);

        // One point every 4 s, each costing 2 exhaustion (half a food point).
        QCOMPARE(run(hunger, 10.0, 4.0), 11.0);
        QCOMPARE(hunger.exhaustion(), 2.0);
        QCOMPARE(hunger.food(), 18);
        QCOMPARE(run(hunger, 11.0, 4.0), 12.0);
        QCOMPARE(hunger.food(), 17);

        // Now below the gate: no more healing.
        QCOMPARE(run(hunger, 12.0, 8.0), 12.0);
        // Damage restarts the timer.
        hunger.restore(19, 0.0, 0.0);
        QCOMPARE(run(hunger, 12.0, 3.0), 12.0);
        hunger.hurt();
        QCOMPARE(run(hunger, 12.0, 3.0), 12.0);
        QCOMPARE(run(hunger, 12.0, 1.0), 13.0);

        // Full health needs no healing and costs nothing.
        hunger.restore(20, 0.0, 5.0);
        QCOMPARE(run(hunger, 20.0, 8.0), 20.0);
        QCOMPARE(hunger.exhaustion(), 0.0);
    }

    // At a full bar with saturation the player heals a point every 0.5 s, paid from the
    // saturation; a step with less than 6 saturation heals a sixth of a point per point of it.
    void fullBarWithSaturationHealsFast()
    {
        QcHunger hunger;
        hunger.restore(20, 0.0, 12.0);
        QCOMPARE(run(hunger, 10.0, 0.45), 10.0);
        QCOMPARE(run(hunger, 10.0, 0.05), 11.0);
        QCOMPARE(hunger.saturation(), 11.0);
        QCOMPARE(hunger.exhaustion(), 2.0);
        // Seven more steps in 3.5 s heal six points; the bar stays full.
        QCOMPARE(run(hunger, 11.0, 3.5), 17.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), 2.0);
        QCOMPARE(hunger.exhaustion(), 2.0);

        // A small rest heals a part of a point.
        hunger.restore(20, 0.0, 3.0);
        QCOMPARE(run(hunger, 10.0, 0.5), 10.5);
        QCOMPARE(hunger.exhaustion(), 3.0);

        // Without saturation, or below a full bar, the slow regeneration takes over.
        hunger.restore(19, 0.0, 5.0);
        QCOMPARE(run(hunger, 10.0, 3.5), 10.0);
        QCOMPARE(run(hunger, 10.0, 0.5), 11.0);
        QCOMPARE(hunger.food(), 19);
        QCOMPARE(hunger.saturation(), 5.0);
        QCOMPARE(hunger.exhaustion(), 2.0);
    }

    void starvationStopsAtTheFloor()
    {
        QcHunger hunger;
        hunger.restore(0, 0.0, 0.0);
        QCOMPARE(run(hunger, 5.0, 4.0), 4.0);
        QCOMPARE(run(hunger, 4.0, 3.5), 4.0);
        // From 4 to the floor of 1 takes 3 x 4 s and stays there.
        QCOMPARE(run(hunger, 4.0, 60.0), QcHunger::starvationFloor);
        hunger.restore(0, 0.0, 3.0);
        QCOMPARE(hunger.saturation(), 0.0);
        QCOMPARE(run(hunger, 1.5, 8.0), 1.0);
        // One food point ends starving without healing.
        hunger.restore(1, 0.0, 0.0);
        QCOMPARE(run(hunger, 5.0, 20.0), 5.0);
    }

    void restoredValuesAreClamped()
    {
        QcHunger hunger;
        hunger.restore(99, 77.0, 50.0);
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.exhaustion(), QcHunger::exhaustionPerFood);
        QCOMPARE(hunger.saturation(), 20.0);
        hunger.restore(-4, std::nan(""), std::nan(""));
        QCOMPARE(hunger.food(), 0);
        QCOMPARE(hunger.exhaustion(), 0.0);
        QCOMPARE(hunger.saturation(), 0.0);
        hunger.restore(7, 0.0, -3.0);
        QCOMPARE(hunger.saturation(), 0.0);

        // A respawn fills the bar with the saturation of a new player.
        hunger.reset();
        QCOMPARE(hunger.food(), 20);
        QCOMPARE(hunger.saturation(), QcHunger::spawnSaturation);
    }
};

QTEST_GUILESS_MAIN(TestHunger)
#include "test_hunger.moc"

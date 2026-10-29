// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcGameMode.h"
#include "common/physics/QcPhysics.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>

// Creative flight in the shared physics (decision E-284): granted only with QcMovement::MayFly, no
// gravity, up and down with jump and sneak, faster with sprint, landing ends it. A spectator's
// flight (QcMovement::NoClip, decision E-730) goes through blocks at the speed of the wheel.
namespace {

constexpr QcBlockId stone = 1;
constexpr double dt = QcPhysics::tickDuration;

QcPlayerInput flying()
{
    QcPlayerInput input;
    input.flying = true;
    return input;
}

void run(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, int ticks, QcMovement movement)
{
    for (int i = 0; i < ticks; ++i) {
        motion.stepPlayer(input, world, dt, movement);
    }
}

// Height gained in `ticks` ticks from rest when the vertical speed halves its distance to `target`
// every tick (blend = flyAcceleration * dt = 0.5), written out independently of the physics.
double riseFromRest(double target, int ticks)
{
    double speed = 0.0;
    double height = 0.0;
    for (int i = 0; i < ticks; ++i) {
        speed += (target - speed) * 0.5;
        height += speed * 0.05;
    }
    return height;
}

} // namespace

class TestFlight : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content = qcMakeTestContent();

    QcEntityMotion standingOnFloor(QcTestWorld &world) const
    {
        world.fillBox({-40, 63, -40}, {40, 63, 40}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        run(motion, {}, world, 2, QcMovement::Walk);
        return motion;
    }

private slots:
    void survivalIgnoresTheWishToFly()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);

        QcPlayerInput input = flying();
        input.jump = true;
        motion.stepPlayer(input, world, dt, QcMovement::Walk);
        QVERIFY(!motion.flying);

        // An ordinary jump: up at 9 m/s minus one tick of gravity, then back on the floor.
        QCOMPARE(motion.velocity.y, 9.0 - 28.0 * dt);
        run(motion, flying(), world, 40, QcMovement::Walk);
        QVERIFY(!motion.flying);
        QVERIFY(motion.onGround);
        QCOMPARE(motion.position.y, 64.0);
    }

    void jumpRisesWithoutGravity()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);

        QcPlayerInput input = flying();
        input.jump = true;
        run(motion, input, world, 10, QcMovement::MayFly);

        QVERIFY(motion.flying);
        QVERIFY(!motion.onGround);
        QVERIFY(std::abs(motion.position.y - 64.0 - riseFromRest(7.5, 10)) < 1e-9);
        QVERIFY(std::abs(motion.velocity.y - 7.5 * (1.0 - std::pow(0.5, 10))) < 1e-9);
    }

    void hoversWhenNoKeyIsHeld()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);
        QcPlayerInput up = flying();
        up.jump = true;
        run(motion, up, world, 20, QcMovement::MayFly);

        // The rise eases out, then the height stays: no gravity pulls while flying.
        run(motion, flying(), world, 30, QcMovement::MayFly);
        const double height = motion.position.y;
        run(motion, flying(), world, 40, QcMovement::MayFly);
        QVERIFY(motion.flying);
        QVERIFY(std::abs(motion.position.y - height) < 1e-6);
        QVERIFY(std::abs(motion.velocity.y) < 1e-6);
    }

    void flyingIsFasterAndSprintFlyingFasterStill()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);
        QcPlayerInput up = flying();
        up.jump = true;
        run(motion, up, world, 10, QcMovement::MayFly);

        QcPlayerInput forward = flying();
        forward.forward = 1.0f;
        forward.yaw = 0.0f;
        run(motion, forward, world, 40, QcMovement::MayFly);
        // Yaw 0 looks along -z.
        QVERIFY(std::abs(motion.velocity.z + 10.9) < 1e-6);
        QVERIFY(std::abs(motion.velocity.x) < 1e-9);

        forward.sprint = true;
        run(motion, forward, world, 40, QcMovement::MayFly);
        QVERIFY(std::abs(motion.velocity.z + 21.6) < 1e-6);
        QVERIFY(-motion.velocity.z > 2.0 * 10.9 * 0.99);
    }

    void sneakingDescendsAndLandingEndsTheFlight()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);
        QcPlayerInput up = flying();
        up.jump = true;
        run(motion, up, world, 10, QcMovement::MayFly);
        QVERIFY(motion.position.y > 66.0);

        QcPlayerInput down = flying();
        down.sneak = true;
        int ticks = 0;
        while (motion.flying && ticks < 100) {
            motion.stepPlayer(down, world, dt, QcMovement::MayFly);
            ++ticks;
        }

        QVERIFY(!motion.flying);
        QVERIFY(motion.onGround);
        QCOMPARE(motion.position.y, 64.0);
        QCOMPARE(motion.velocity.y, 0.0);
    }

    void losingTheRightToFlyDropsThePlayer()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);
        QcPlayerInput up = flying();
        up.jump = true;
        run(motion, up, world, 10, QcMovement::MayFly);
        const double height = motion.position.y;

        // The game mode changed to survival: the same input no longer flies, gravity pulls.
        run(motion, flying(), world, 10, QcMovement::Walk);
        QVERIFY(!motion.flying);
        QVERIFY(motion.position.y < height);
        run(motion, flying(), world, 60, QcMovement::Walk);
        QVERIFY(motion.onGround);
        QCOMPARE(motion.position.y, 64.0);
    }

    void creativeModeGrantsFlight()
    {
        QVERIFY(QcGameModes::rules(QcGameMode::Creative).mayFly);
        QVERIFY(!QcGameModes::rules(QcGameMode::Survival).mayFly);
    }

    void gameModeNamesParse()
    {
        using QcGameModes::parse;
        QCOMPARE(parse(u"creative"), QcGameMode::Creative);
        QCOMPARE(parse(u" Creative "), QcGameMode::Creative);
        QCOMPARE(parse(u"c"), QcGameMode::Creative);
        QCOMPARE(parse(u"1"), QcGameMode::Creative);
        QCOMPARE(parse(u"SURVIVAL"), QcGameMode::Survival);
        QCOMPARE(parse(u"s"), QcGameMode::Survival);
        QCOMPARE(parse(u"0"), QcGameMode::Survival);
        QCOMPARE(parse(u"Adventure"), QcGameMode::Adventure);
        QCOMPARE(parse(u"a"), QcGameMode::Adventure);
        QCOMPARE(parse(u"2"), QcGameMode::Adventure);
        QCOMPARE(parse(u"Spectator"), QcGameMode::Spectator);
        QCOMPARE(parse(u"sp"), QcGameMode::Spectator);
        QCOMPARE(parse(u"3"), QcGameMode::Spectator);
        QVERIFY(!parse(u"puzzle"));
        QVERIFY(!parse(u"4"));
        QVERIFY(!parse(u""));

        QCOMPARE(QcGameModes::fromValue(1), QcGameMode::Creative);
        QCOMPARE(QcGameModes::fromValue(2), QcGameMode::Adventure);
        QCOMPARE(QcGameModes::fromValue(3), QcGameMode::Spectator);
        QVERIFY(!QcGameModes::fromValue(4));
        QVERIFY(!QcGameModes::fromValue(255));
        QCOMPARE(QcGameModes::name(QcGameMode::Creative), QLatin1StringView("creative"));
        QCOMPARE(QcGameModes::name(QcGameMode::Spectator), QLatin1StringView("spectator"));
        QVERIFY(QcGameModes::inventorySet(QcGameMode::Creative) != QcGameModes::inventorySet(QcGameMode::Survival));
        QCOMPARE(QcGameModes::inventorySet(QcGameMode::Spectator), QcGameModes::inventorySet(QcGameMode::Survival));
    }

    void spectatorRules()
    {
        const QcGameModes::Rules spectator = QcGameModes::rules(QcGameMode::Spectator);
        QCOMPARE(spectator.movement(), QcMovement::NoClip);
        QVERIFY(spectator.invulnerable && spectator.hungerFrozen);
        QVERIFY(!spectator.interacts && !spectator.tangible);
        QVERIFY(!spectator.instantBreak && !spectator.infiniteItems && !spectator.itemCatalogue);
        QCOMPARE(QcGameModes::rules(QcGameMode::Creative).movement(), QcMovement::MayFly);
        QCOMPARE(QcGameModes::rules(QcGameMode::Survival).movement(), QcMovement::Walk);
        QVERIFY(QcGameModes::rules(QcGameMode::Creative).interacts && QcGameModes::rules(QcGameMode::Creative).tangible);
    }

    void spectatorFliesThroughBlocks()
    {
        QcTestWorld world(m_content);
        world.fillBox({-10, 50, -10}, {10, 80, 10}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};

        // No gravity inside the stone, straight through it forwards (yaw 0 looks along -z) and up.
        run(motion, {}, world, 20, QcMovement::NoClip);
        QCOMPARE(motion.position.y, 64.0);
        QVERIFY(motion.flying);
        QVERIFY(!motion.onGround && !motion.inWater);
        QcPlayerInput forward;
        forward.forward = 1.0f;
        forward.jump = true;
        run(motion, forward, world, 40, QcMovement::NoClip);
        QVERIFY(motion.position.z < -15.0);
        QVERIFY(motion.position.y > 75.0);

        // The same input on foot stays outside the stone.
        QcEntityMotion walker;
        walker.position = {0.5, 81.0, 0.5};
        run(walker, forward, world, 40, QcMovement::Walk);
        QVERIFY(walker.position.y >= 81.0);
    }

    void spectatorFlightSpeedFollowsTheWheel()
    {
        QcTestWorld world(m_content);
        QcPlayerInput forward;
        forward.forward = 1.0f;
        const auto distance = [&](quint8 speed) {
            QcPlayerInput input = forward;
            input.flySpeed = speed;
            QcEntityMotion motion;
            motion.position = {0.5, 100.0, 0.5};
            run(motion, input, world, 60, QcMovement::NoClip);
            return 0.5 - motion.position.z;
        };
        const double normal = distance(QcPlayerInput::normalFlySpeed);
        QVERIFY(std::abs(normal - QcPhysics::flySpeed * 60 * dt) < 1.0);
        QVERIFY(std::abs(distance(20) - 2.0 * normal) < 1e-6 * normal);
        QVERIFY(std::abs(distance(5) - 0.5 * normal) < 1e-6 * normal);
        // Out of range clamps to the range.
        QCOMPARE(distance(200), distance(QcPlayerInput::maxFlySpeed));
    }

    void spectatorStopsAtTheVoidFloor()
    {
        QcTestWorld world(m_content);
        QcPlayerInput down;
        down.sneak = true;
        QcEntityMotion motion;
        motion.position = {0.5, QcWorld::voidFloorY + 1.0, 0.5};
        run(motion, down, world, 40, QcMovement::NoClip);
        QCOMPARE(motion.position.y, QcWorld::voidFloorY);
    }
};

QTEST_GUILESS_MAIN(TestFlight)
#include "test_flight.moc"

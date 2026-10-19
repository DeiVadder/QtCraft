#include "common/gameplay/QcGameMode.h"
#include "common/physics/QcPhysics.h"
#include "QcTestWorld.h"

#include <QTest>

#include <cmath>

// Creative flight in the shared physics (decision E-284): granted only with `mayFly`, no gravity,
// up and down with jump and sneak, faster with sprint, landing ends it.
namespace {

constexpr QcBlockId stone = 1;
constexpr double dt = QcPhysics::tickDuration;

QcPlayerInput flying()
{
    QcPlayerInput input;
    input.flying = true;
    return input;
}

void run(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, int ticks, bool mayFly)
{
    for (int i = 0; i < ticks; ++i) {
        motion.stepPlayer(input, world, dt, mayFly);
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
        run(motion, {}, world, 2, false);
        return motion;
    }

private slots:
    void survivalIgnoresTheWishToFly()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);

        QcPlayerInput input = flying();
        input.jump = true;
        motion.stepPlayer(input, world, dt, false);
        QVERIFY(!motion.flying);

        // An ordinary jump: up at 9 m/s minus one tick of gravity, then back on the floor.
        QCOMPARE(motion.velocity.y, 9.0 - 28.0 * dt);
        run(motion, flying(), world, 40, false);
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
        run(motion, input, world, 10, true);

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
        run(motion, up, world, 20, true);

        // The rise eases out, then the height stays: no gravity pulls while flying.
        run(motion, flying(), world, 30, true);
        const double height = motion.position.y;
        run(motion, flying(), world, 40, true);
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
        run(motion, up, world, 10, true);

        QcPlayerInput forward = flying();
        forward.forward = 1.0f;
        forward.yaw = 0.0f;
        run(motion, forward, world, 40, true);
        // Yaw 0 looks along -z.
        QVERIFY(std::abs(motion.velocity.z + 10.9) < 1e-6);
        QVERIFY(std::abs(motion.velocity.x) < 1e-9);

        forward.sprint = true;
        run(motion, forward, world, 40, true);
        QVERIFY(std::abs(motion.velocity.z + 21.6) < 1e-6);
        QVERIFY(-motion.velocity.z > 2.0 * 10.9 * 0.99);
    }

    void sneakingDescendsAndLandingEndsTheFlight()
    {
        QcTestWorld world(m_content);
        QcEntityMotion motion = standingOnFloor(world);
        QcPlayerInput up = flying();
        up.jump = true;
        run(motion, up, world, 10, true);
        QVERIFY(motion.position.y > 66.0);

        QcPlayerInput down = flying();
        down.sneak = true;
        int ticks = 0;
        while (motion.flying && ticks < 100) {
            motion.stepPlayer(down, world, dt, true);
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
        run(motion, up, world, 10, true);
        const double height = motion.position.y;

        // The game mode changed to survival: the same input no longer flies, gravity pulls.
        run(motion, flying(), world, 10, false);
        QVERIFY(!motion.flying);
        QVERIFY(motion.position.y < height);
        run(motion, flying(), world, 60, false);
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
        QVERIFY(!parse(u"spectator"));
        QVERIFY(!parse(u"2"));
        QVERIFY(!parse(u""));

        QCOMPARE(QcGameModes::fromValue(1), QcGameMode::Creative);
        QVERIFY(!QcGameModes::fromValue(2));
        QCOMPARE(QcGameModes::name(QcGameMode::Creative), QLatin1StringView("creative"));
        QVERIFY(QcGameModes::inventorySet(QcGameMode::Creative) != QcGameModes::inventorySet(QcGameMode::Survival));
    }
};

QTEST_GUILESS_MAIN(TestFlight)
#include "test_flight.moc"

#include "QcPhysics.h"
#include "QcTestWorld.h"

#include <QTest>

namespace {

constexpr QcBlockId stone = 1;
constexpr QcBlockId water = 2;
constexpr double dt = QcPhysics::tickDuration;

void run(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        QcPhysics::stepPlayer(motion, input, world, dt);
    }
}

} // namespace

class TestPhysics : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content = qcMakeTestContent();

private slots:
    void fallsAndLandsExactlyOnFloor()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 63, -3}, {3, 63, 3}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 70.0, 0.5};
        run(motion, {}, world, 60);
        QCOMPARE(motion.position.y, 64.0);
        QVERIFY(motion.onGround);
        QCOMPARE(motion.velocity.y, 0.0);
    }

    void jumpApexMatchesHandComputedHeight()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 63, -3}, {3, 63, 3}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        run(motion, {}, world, 2);
        QVERIFY(motion.onGround);
        QcPlayerInput jump;
        jump.jump = true;
        double apex = 0.0;
        QcPhysics::stepPlayer(motion, jump, world, dt);
        for (int i = 0; i < 30; ++i) {
            apex = std::max(apex, motion.position.y - 64.0);
            QcPhysics::stepPlayer(motion, {}, world, dt);
        }
        // v0 = 9.0, g = 28: per-tick speeds 7.6, 6.2, 4.8, 3.4, 2.0, 0.6 -> 23.6... * 0.05 = 1.23 m.
        QVERIFY2(std::abs(apex - 1.23) < 1e-9, qPrintable(QString::number(apex, 'f', 6)));
        QCOMPARE(motion.position.y, 64.0);
    }

    void wallStopsHorizontalMovement()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 63, -3}, {6, 63, 3}, stone);
        world.fillBox({3, 64, -3}, {3, 66, 3}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        run(motion, east, world, 40);
        // The wall's west face is at x = 3; the player is 0.6 m wide.
        QCOMPARE(motion.position.x, 2.7);
        QCOMPARE(motion.position.z, 0.5);
    }

    void walkingSpeedIsReachedOnGround()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 63, -30}, {3, 63, 3}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        run(motion, {}, world, 1);
        QcPlayerInput north;
        north.forward = 1.0f;
        run(motion, north, world, 20);
        // Ground acceleration reaches full speed within one tick (20/s * 0.05 s), so 20 ticks cover 4.3 m.
        QVERIFY(std::abs((0.5 - motion.position.z) - 4.3) < 1e-9);
    }

    void cannotWalkUpOneBlockButCanJumpOnIt()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 63, -3}, {8, 63, 3}, stone);
        world.fillBox({3, 64, -3}, {8, 64, 3}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        run(motion, east, world, 30);
        QCOMPARE(motion.position.y, 64.0);
        QCOMPARE(motion.position.x, 2.7);
        east.jump = true;
        run(motion, east, world, 12);
        east.jump = false;
        run(motion, east, world, 20);
        QCOMPARE(motion.position.y, 65.0);
        QVERIFY(motion.position.x > 3.3);
    }

    void unloadedChunksActLikeSolidGround()
    {
        QcTestWorld world(m_content);
        world.loadedRadius = 5;
        QcEntityMotion motion;
        motion.position = {100.5, 80.0, 0.5};
        run(motion, {}, world, 20);
        QCOMPARE(motion.position.y, 80.0);
    }

    void sinksSlowlyInWaterAndSwimsUp()
    {
        QcTestWorld world(m_content);
        world.fillBox({-3, 50, -3}, {3, 50, 3}, stone);
        world.fillBox({-3, 51, -3}, {3, 70, 3}, water);
        QcEntityMotion motion;
        motion.position = {0.5, 65.0, 0.5};
        run(motion, {}, world, 20);
        QVERIFY(motion.inWater);
        QVERIFY(motion.velocity.y >= -QcPhysics::waterSinkSpeed);
        const double sunk = 65.0 - motion.position.y;
        QVERIFY2(sunk > 0.5 && sunk <= QcPhysics::waterSinkSpeed * 1.0 + 1e-9, qPrintable(QString::number(sunk)));
        QcPlayerInput swim;
        swim.jump = true;
        const double before = motion.position.y;
        run(motion, swim, world, 20);
        QVERIFY(motion.position.y > before + 1.0);
    }
};

QTEST_GUILESS_MAIN(TestPhysics)
#include "test_physics.moc"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "QcTestWorld.h"

#include <QTest>

namespace {

constexpr QcBlockId stone = 1;
constexpr QcBlockId water = 2;
constexpr double dt = QcPhysics::tickDuration;

void run(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        motion.stepPlayer(input, world, dt, false);
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

    void nothingHoldsABodyBelowTheWorld()
    {
        // No invisible floor at the bottom (decision E-288): a body falls through y = 0 at the
        // speed of an ordinary fall, also where its column is not loaded.
        QcTestWorld world(m_content);
        world.loadedRadius = 0;
        QcEntityMotion motion;
        motion.position = {0.5, 3.0, 0.5};
        run(motion, {}, world, 40);

        QVERIFY2(motion.position.y < -20.0, qPrintable(QString::number(motion.position.y)));
        QVERIFY(!motion.onGround);
        QVERIFY(!QcPhysics::isSolidAt(world, {0, -1, 0}));
        QVERIFY(!QcPhysics::isSolidAt(world, {5, -1, 5}));
        QVERIFY(QcPhysics::collisionBoxesAt(world, {5, -1, 5}).isEmpty());

        // Unloaded chunks inside the world's height stay walls (decision E-284).
        QVERIFY(QcPhysics::isSolidAt(world, {5, 0, 5}));
        QCOMPARE(QcPhysics::collisionBoxesAt(world, {5, 0, 5}).size(), 1);

        // Far below, a body stops sinking without standing on anything (a world whose time stands
        // still would otherwise let a player fall for ever); it can still rise from there.
        run(motion, {}, world, 400);
        QCOMPARE(motion.position.y, QcWorld::voidFloorY);
        QCOMPARE(motion.velocity.y, 0.0);
        QVERIFY(!motion.onGround);
        QcPlayerInput up;
        up.flying = true;
        up.jump = true;
        motion.stepPlayer(up, world, dt, true);
        QVERIFY(motion.position.y > QcWorld::voidFloorY);
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
        motion.stepPlayer(jump, world, dt, false);
        for (int i = 0; i < 30; ++i) {
            apex = std::max(apex, motion.position.y - 64.0);
            motion.stepPlayer({}, world, dt, false);
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

    void roundingInsideAnObstacleStillCountsAsTouching()
    {
        // A body that ended a clipped move a rounding error inside a post (feet minus half width
        // does not give the face back exactly) is still stopped by it ...
        const QcAabb post{{0.375, 0.0, 0.375}, {0.625, 1.5, 0.625}};
        const QcAabb body{{0.625 - 1e-12, 0.0, 0.1}, {1.425, 0.8, 0.9}};
        const double intoPost = body.clipX(post, -0.04);
        QVERIFY2(intoPost >= 0.0, qPrintable(QString::number(intoPost)));

        // ... and one sliding along a wall a rounding error inside it is not stopped by the seam to
        // the next block of the wall.
        const QcAabb nextBlock{{1.0, 0.0, -1.0}, {2.0, 1.0, 0.0}};
        const QcAabb sliding{{0.2, 0.0, -1e-12}, {0.8, 1.8, 0.6}};
        const double alongWall = sliding.clipX(nextBlock, 0.5);
        QCOMPARE(alongWall, 0.5);
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

    // Sprinting is 30 % faster than walking, as in the model (4.317 and 5.612 m/s), on the ground
    // and while jumping along (decision E-280). Measured over 4 s after a second to reach speed.
    void sprintIsThirtyPercentFasterThanWalking()
    {
        QcTestWorld world(m_content);
        world.fillBox({-2, 63, -120}, {2, 63, 2}, stone);
        const auto speedOf = [&world](bool sprint, bool jump) {
            QcEntityMotion motion;
            motion.position = {0.5, 64.0, 0.5};
            run(motion, {}, world, 1);
            QcPlayerInput input;
            input.forward = 1.0f;
            input.sprint = sprint;
            input.jump = jump;
            run(motion, input, world, 20);
            const double start = motion.position.z;
            run(motion, input, world, 80);
            return (start - motion.position.z) / 4.0;
        };

        const double walk = speedOf(false, false);
        const double sprint = speedOf(true, false);
        const double sprintJump = speedOf(true, true);
        QVERIFY2(std::abs(walk - QcPhysics::walkSpeed) < 1e-6, qPrintable(QString::number(walk)));
        QVERIFY2(std::abs(sprint - QcPhysics::sprintSpeed) < 1e-6, qPrintable(QString::number(sprint)));
        QVERIFY2(std::abs(sprint / walk - 1.3) < 0.01, qPrintable(QString::number(sprint / walk)));
        // Jumping does not slow a sprint down.
        QVERIFY2(sprintJump >= sprint - 1e-6, qPrintable(QString::number(sprintJump)));
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

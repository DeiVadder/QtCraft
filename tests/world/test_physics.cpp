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
        motion.stepPlayer(input, world, dt, QcMovement::Walk);
    }
}

// A pool of `liquid` west of x = 3 with its surface at y = 63 (cells up to 62) over a stone floor
// at `floorY`, and a stone bank from x = 3 eastwards whose top lies `bankHeight` blocks above the
// liquid's top cell.
QcTestWorld shore(const QcContentRegistry &content, QcBlockId liquid, int floorY, int bankHeight)
{
    QcTestWorld world(content);
    world.fillBox({-6, floorY, -3}, {8, floorY, 3}, stone);
    world.fillBox({-6, floorY + 1, -3}, {2, 62, 3}, liquid);
    world.fillBox({3, floorY + 1, -3}, {8, 61 + bankHeight, 3}, stone);
    return world;
}

// Swims east at the bank, holding forward and jump, for at most `ticks`; the motion afterwards.
QcEntityMotion swimToBank(const QcBlockAccess &world, const QcVec3 &start, int ticks)
{
    QcEntityMotion motion;
    motion.position = start;
    QcPlayerInput east;
    east.yaw = 90.0f;
    east.forward = 1.0f;
    east.jump = true;
    for (int i = 0; i < ticks && !(motion.onGround && motion.position.x > 3.3); ++i) {
        motion.stepPlayer(east, world, dt, QcMovement::Walk);
    }
    return motion;
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
        motion.stepPlayer(up, world, dt, QcMovement::MayFly);
        QVERIFY(motion.position.y > QcWorld::voidFloorY);
    }

    void bodiesStopAtTheBoundOfTheWorld()
    {
        // The technical bound (decision E-883) holds every body, also one that passes through blocks
        // (a spectator) or falls through the void, where nothing else would stop it.
        QcTestWorld world(m_content);
        world.loadedRadius = 0;
        // Both sides stop at the centre of the outermost block column: -maxHorizontalBlock on the
        // negative side, maxChunkBlock (the last column of the edge chunk) on the positive side.
        constexpr double low = QcWorld::lowestBody;
        constexpr double high = QcWorld::highestBody;
        static_assert(low == -QcWorld::maxHorizontalBlock + 0.5 && high == QcWorld::maxChunkBlock + 0.5);
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        QcEntityMotion spectator;
        spectator.position = {high - 2.0, 70.0, low};
        for (int i = 0; i < 40; ++i) {
            spectator.stepPlayer(east, world, dt, QcMovement::NoClip);
        }
        QCOMPARE(spectator.position.x, high);
        QCOMPARE(spectator.velocity.x, 0.0);
        QCOMPARE(spectator.position.z, low);

        QcPlayerInput west = east;
        west.yaw = -90.0f;
        QcEntityMotion falling;
        falling.position = {low + 1.0, -10.0, 0.5};
        run(falling, west, world, 40);
        QCOMPARE(falling.position.x, low);
        QVERIFY(falling.position.y < -10.0);
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
        motion.stepPlayer(jump, world, dt, QcMovement::Walk);
        for (int i = 0; i < 30; ++i) {
            apex = std::max(apex, motion.position.y - 64.0);
            motion.stepPlayer({}, world, dt, QcMovement::Walk);
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

    // The water exit (decision E-918): a swimmer at a bank one block above the water's top cell,
    // holding forward and jump, climbs out within a few seconds, from deep water, from the floor of
    // shallow water and from lava; a bank two blocks high stays out of reach.
    void climbsOutOfLiquidOntoABankOneBlockHigh()
    {
        const QcTestWorld deep = shore(m_content, water, 50, 1);
        const QcEntityMotion fromDeep = swimToBank(deep, {0.5, 58.0, 0.5}, 100);
        QVERIFY2(fromDeep.onGround && fromDeep.position.y == 63.0 && fromDeep.position.x > 3.3,
                 qPrintable(QStringLiteral("%1 %2").arg(fromDeep.position.x).arg(fromDeep.position.y)));
        QVERIFY(!fromDeep.inWater);

        // Standing on the floor of water one block deep (no swimming needed to reach the bank).
        const QcTestWorld shallow = shore(m_content, water, 61, 1);
        const QcEntityMotion fromShallow = swimToBank(shallow, {1.5, 62.0, 0.5}, 60);
        QVERIFY2(fromShallow.onGround && fromShallow.position.y == 63.0 && fromShallow.position.x > 3.3,
                 qPrintable(QStringLiteral("%1 %2").arg(fromShallow.position.x).arg(fromShallow.position.y)));

        // Lava slows every movement (factor 0.45 like the content's lava), the exit still works.
        QcContentRegistry content = qcMakeTestContent();
        QcBlockDefinition lava;
        lava.name = QStringLiteral("test:lava");
        lava.solid = false;
        lava.opaque = false;
        lava.liquid = true;
        lava.replaceable = true;
        lava.flow.movementFactor = 0.45;
        lava.fire.setsAlightSeconds = 15;
        const QcResult<QcBlockId> lavaId = content.registerBlock(lava);
        QVERIFY(lavaId.has_value());
        const QcTestWorld lavaPool = shore(content, *lavaId, 55, 1);
        const QcEntityMotion fromLava = swimToBank(lavaPool, {0.5, 58.0, 0.5}, 200);
        QVERIFY2(fromLava.onGround && fromLava.position.y == 63.0 && fromLava.position.x > 3.3,
                 qPrintable(QStringLiteral("%1 %2").arg(fromLava.position.x).arg(fromLava.position.y)));

        // Two blocks above the water: no way out by swimming (as in the reference game).
        const QcTestWorld high = shore(m_content, water, 50, 2);
        const QcEntityMotion atHigh = swimToBank(high, {0.5, 58.0, 0.5}, 200);
        QVERIFY2(atHigh.position.x < 3.0 && atHigh.position.y < 64.0,
                 qPrintable(QStringLiteral("%1 %2").arg(atHigh.position.x).arg(atHigh.position.y)));
    }

    void fallingIntoTheWaterBesideABankLandsInIt()
    {
        // A body falling into the top water cell beside the bank, forward held into it, is not
        // thrown out (decision E-918): from every height between 6 and 9 m (1/32 m apart, so some
        // tick ends with the feet in the top part of the cell) it sinks into the water.
        const QcTestWorld world = shore(m_content, water, 50, 1);
        QcPlayerInput east;
        east.yaw = 90.0f;
        for (int step = 0; step < 96; ++step) {
            QcEntityMotion motion;
            motion.position = {2.69, 69.0 + step / 32.0, 0.5};
            double lowest = motion.position.y;
            bool rose = false;
            for (int tick = 0; tick < 40 && lowest > 61.0; ++tick) {
                // Forward only below the bank's top: in the air it would carry the body onto it.
                east.forward = motion.position.y < 63.0 ? 1.0f : 0.0f;
                motion.stepPlayer(east, world, dt, QcMovement::Walk);
                lowest = std::min(lowest, motion.position.y);
                rose = rose || motion.velocity.y > 0.0;
            }
            QVERIFY2(lowest < 62.0 && !rose, qPrintable(QStringLiteral("start %1: lowest %2").arg(69.0 + step / 32.0).arg(lowest)));
        }
    }

    void noBouncingAtABankTwoBlocksHigh()
    {
        // The way out must be free 0.6 m above the body: at a high bank the swimmer bobs at the
        // surface as in open water, never lifted (feet at most 0.76 above the cell's floor).
        const QcTestWorld high = shore(m_content, water, 50, 2);
        QcEntityMotion motion;
        motion.position = {0.5, 58.0, 0.5};
        QcPlayerInput east;
        east.yaw = 90.0f;
        east.forward = 1.0f;
        east.jump = true;
        double highest = motion.position.y;
        for (int tick = 0; tick < 200; ++tick) {
            motion.stepPlayer(east, high, dt, QcMovement::Walk);
            highest = std::max(highest, motion.position.y);
        }
        QVERIFY2(highest < 62.8, qPrintable(QString::number(highest)));
    }

    void wadingWithoutABankDoesNotLift()
    {
        // Bobbing at the surface in open water, swimming forward, nothing lifts the body.
        QcTestWorld world(m_content);
        world.fillBox({-20, 50, -3}, {20, 50, 3}, stone);
        world.fillBox({-20, 51, -3}, {20, 62, 3}, water);
        QcEntityMotion motion = swimToBank(world, {-15.5, 60.0, 0.5}, 100);
        QVERIFY2(motion.position.y < 63.0, qPrintable(QString::number(motion.position.y)));
        QVERIFY(motion.velocity.y < QcPhysics::liquidExitSpeed - 1.0);
    }
};

QTEST_GUILESS_MAIN(TestPhysics)
#include "test_physics.moc"

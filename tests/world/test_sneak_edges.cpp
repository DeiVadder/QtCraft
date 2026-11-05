// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPhysics.h"
#include "fixtures/QcTestUtil.h"
#include "fixtures/QcTestWorld.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr double dt = QcPhysics::tickDuration;
// The half width of the player's box: how far it may hang over an edge and still stand on it.
constexpr double halfWidth = QcPhysics::playerWidth * 0.5;

// Facing east (+x) and north (-z), as QcEntityMotion::stepPlayer turns the yaw.
constexpr float east = 90.0f;
constexpr float north = 0.0f;

QcPlayerInput sneaking(float yaw, float forward = 1.0f, float strafe = 0.0f)
{
    QcPlayerInput input;
    input.yaw = yaw;
    input.forward = forward;
    input.strafe = strafe;
    input.sneak = true;
    return input;
}

QcEntityMotion standingAt(const QcVec3 &feet)
{
    QcEntityMotion motion;
    motion.position = feet;
    motion.onGround = true;
    return motion;
}

QcEntityMotion run(QcEntityMotion motion, const QcPlayerInput &input, const QcBlockAccess &world, int ticks, QcMovement movement = QcMovement::Walk)
{
    for (int i = 0; i < ticks; ++i) {
        motion.stepPlayer(input, world, dt, movement);
    }
    return motion;
}

} // namespace

// The edge guard of a sneaking player (decision E-961) in the shared physics: on the ground he
// stops where the next step would leave him nothing to stand on within QcPhysics::sneakEdgeDepth,
// each axis on its own, so he can walk along an edge and step down a slab, also in shallow water and
// on ice; flying, swimming, climbing and in the air nothing holds him back.
class TestSneakEdges : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // The block state `state` of the base content ("stone", "stone_slab[type=bottom]").
    QcBlockId block(const QString &state) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(u"qtcraft:"_s + state);
        if (id) {
            return *id;
        }
        QcTestUtil::failCase(u"unknown block state %1"_s.arg(state));
        return qcAirBlockId;
    }

    // A stone platform from x -4 to 0 and z -4 to 4 at y = 63 (standing height 64), ten blocks
    // above a stone floor: its east edge lies at x = 1.
    QcTestWorld platform() const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-8, 53, -8}, {8, 53, 8}, block(u"stone"_s));
        world.fillBox({-4, 63, -4}, {0, 63, 4}, block(u"stone"_s));
        return world;
    }

private slots:
    void sneakingStopsAtAnEdgeOnEachAxis()
    {
        const QcTestWorld world = platform();
        const QcEntityMotion atEast = run(standingAt({0.5, 64.0, 0.5}), sneaking(east), world, 60);
        QVERIFY2(atEast.position.x > 1.0 + halfWidth - QcPhysics::sneakEdgeStep && atEast.position.x <= 1.0 + halfWidth, qPrintable(QString::number(atEast.position.x)));
        QCOMPARE(atEast.position.y, 64.0);
        QVERIFY(atEast.onGround);
        QCOMPARE(atEast.velocity.x, 0.0);

        // The north edge of the platform lies at z = -4.
        const QcEntityMotion atNorth = run(standingAt({-1.5, 64.0, 0.5}), sneaking(north), world, 100);
        QVERIFY2(atNorth.position.z < -4.0 - halfWidth + QcPhysics::sneakEdgeStep && atNorth.position.z >= -4.0 - halfWidth, qPrintable(QString::number(atNorth.position.z)));
        QCOMPARE(atNorth.position.y, 64.0);

        // Without sneaking the same walk drops off the edge.
        QcPlayerInput walking = sneaking(east);
        walking.sneak = false;
        const QcEntityMotion fallen = run(standingAt({0.5, 64.0, 0.5}), walking, world, 60);
        QVERIFY2(fallen.position.y < 60.0, qPrintable(QString::number(fallen.position.y)));
    }

    void sneakingWalksAlongAnEdge()
    {
        // Diagonally north-east at the east edge: x stops there, z goes on.
        const QcTestWorld world = platform();
        const QcEntityMotion start = run(standingAt({0.5, 64.0, 3.0}), sneaking(east), world, 30);
        QVERIFY(start.position.x > 1.0);
        const QcEntityMotion along = run(start, sneaking(east, 1.0f, -1.0f), world, 40);
        QVERIFY2(along.position.x > 1.0 + halfWidth - QcPhysics::sneakEdgeStep && along.position.x <= 1.0 + halfWidth, qPrintable(QString::number(along.position.x)));
        QVERIFY2(along.position.z < start.position.z - 1.0, qPrintable(QString::number(along.position.z)));
        QCOMPARE(along.position.y, 64.0);
    }

    void sneakingStepsDownASlab()
    {
        // A bottom slab east of the platform: half a block down is no edge, its own east side is.
        QcTestWorld world = platform();
        world.fillBox({1, 63, -4}, {1, 63, 4}, block(u"stone_slab[type=bottom]"_s));
        const QcEntityMotion moved = run(standingAt({0.5, 64.0, 0.5}), sneaking(east), world, 80);
        QCOMPARE(moved.position.y, 63.5);
        QVERIFY2(moved.position.x > 2.0 + halfWidth - QcPhysics::sneakEdgeStep && moved.position.x <= 2.0 + halfWidth, qPrintable(QString::number(moved.position.x)));
    }

    void nothingHoldsBackAJumperOrAFallingBody()
    {
        const QcTestWorld world = platform();
        // Falling past the edge (no ground under the feet) the body drifts on and falls deep.
        QcEntityMotion falling;
        falling.position = {1.0, 66.0, 0.5};
        falling.velocity = {QcPhysics::sneakSpeed, 0.0, 0.0};
        const QcEntityMotion fell = run(falling, sneaking(east), world, 40);
        QVERIFY2(fell.position.y < 60.0, qPrintable(QString::number(fell.position.y)));

        // A jump off the edge while sneaking is not held back either.
        QcPlayerInput jump = sneaking(east);
        jump.jump = true;
        const QcEntityMotion jumped = run(standingAt({0.9, 64.0, 0.5}), jump, world, 40);
        QVERIFY2(jumped.position.y < 60.0, qPrintable(QString::number(jumped.position.y)));
    }

    void nothingHoldsBackAFlyer()
    {
        const QcTestWorld world = platform();
        QcPlayerInput fly = sneaking(east);
        fly.flying = true;
        const QcEntityMotion flown = run(standingAt({0.5, 64.0, 0.5}), fly, world, 30, QcMovement::MayFly);
        QVERIFY2(flown.position.x > 2.0, qPrintable(QString::number(flown.position.x)));
    }

    void sneakingStopsInShallowWater()
    {
        // A layer of water a block deep at the feet, on the platform and on over the drop (the test
        // world holds it there): the body stands in it, and the edge still holds, as in the role
        // model, where the guard needs only the ground under the feet.
        QcTestWorld world = platform();
        world.fillBox({-4, 64, -4}, {4, 64, 4}, block(u"water"_s));
        const QcEntityMotion waded = run(standingAt({0.5, 64.0, 0.5}), sneaking(east), world, 60);
        QVERIFY(waded.inWater);
        QCOMPARE(waded.position.y, 64.0);
        QVERIFY2(waded.position.x > 1.0 + halfWidth - QcPhysics::sneakEdgeStep && waded.position.x <= 1.0 + halfWidth,
                 qPrintable(QString::number(waded.position.x)));
    }

    void nothingHoldsBackASwimmer()
    {
        // The platform deep under water: a swimmer floating above it drifts past its edge.
        QcTestWorld world = platform();
        world.fillBox({-4, 54, -4}, {4, 67, 4}, block(u"water"_s));
        world.fillBox({-4, 63, -4}, {0, 63, 4}, block(u"stone"_s));
        QcEntityMotion floating;
        floating.position = {0.5, 65.5, 0.5};
        const QcEntityMotion swum = run(floating, sneaking(east), world, 60);
        QVERIFY2(swum.position.x > 1.0 + halfWidth, qPrintable(QString::number(swum.position.x)));
        QVERIFY2(swum.position.y < 64.0, qPrintable(QString::number(swum.position.y)));
    }

    void sneakingHoldsOnIceWithMomentum()
    {
        // A sprint's speed on blue ice: sneaking still stops at the edge instead of sliding off.
        QcTestWorld world = platform();
        world.fillBox({-4, 63, -4}, {0, 63, 4}, block(u"blue_ice"_s));
        QcEntityMotion sliding = standingAt({0.0, 64.0, 0.5});
        sliding.velocity = {QcPhysics::sprintSpeed, 0.0, 0.0};
        const QcEntityMotion held = run(sliding, sneaking(east), world, 60);
        QCOMPARE(held.position.y, 64.0);
        QVERIFY2(held.position.x > 1.0 + halfWidth - QcPhysics::sneakEdgeStep && held.position.x <= 1.0 + halfWidth,
                 qPrintable(QString::number(held.position.x)));
    }

    void sneakingStopsInAnOuterCorner()
    {
        // Diagonally north-east into the platform's corner at x = 1, z = -4: both axes stop.
        const QcTestWorld world = platform();
        const QcEntityMotion cornered = run(standingAt({0.0, 64.0, -3.0}), sneaking(east, 1.0f, -1.0f), world, 80);
        QCOMPARE(cornered.position.y, 64.0);
        QVERIFY2(cornered.position.x > 1.0 && cornered.position.x <= 1.0 + halfWidth, qPrintable(QString::number(cornered.position.x)));
        QVERIFY2(cornered.position.z < -4.0 && cornered.position.z >= -4.0 - halfWidth, qPrintable(QString::number(cornered.position.z)));
    }

    void nothingHoldsBackAClimber()
    {
        // A ladder in the cell at the edge: the climber may step off it.
        QcTestWorld world = platform();
        world.fillBox({0, 64, -4}, {0, 65, 4}, block(u"ladder"_s));
        const QcEntityMotion climbed = run(standingAt({0.5, 64.0, 0.5}), sneaking(east), world, 60);
        QVERIFY2(climbed.position.x > 1.0 + halfWidth, qPrintable(QString::number(climbed.position.x)));
    }
};

QTEST_GUILESS_MAIN(TestSneakEdges)
#include "test_sneak_edges.moc"

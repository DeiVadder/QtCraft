// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcBodyGrid.h"
#include "common/physics/QcBodyPush.h"
#include "common/physics/QcPhysics.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"

#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QcBlockId stone = 1;
constexpr double dt = QcPhysics::tickDuration;

QcBodyPush::Body body(double x, double z, double halfWidth = 0.3, double y = 64.0, double height = 1.8)
{
    return {{x, y, z}, halfWidth, height};
}

} // namespace

// The soft collision between players and creatures (decision E-882): the push of one body on another,
// the grid that sums the pushes of the bodies around one, and the step of a pushed body (also on
// ice and up a slab).
class TestBodyPush : public QObject
{
    Q_OBJECT

private:
    QcContentRegistry m_content = qcMakeTestContent();
    // For ice and slabs.
    std::shared_ptr<const QcContentRegistry> m_base = QcTestUtil::loadContent();

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> found = m_base->blockId(name);
        if (!found) {
            QcTestUtil::failCase(u"Test uses the block %1, which the content does not have"_s.arg(name));
            return qcAirBlockId;
        }
        return *found;
    }

private slots:
    void pushesApartAlongTheLineByTheShareOfTheOverlap()
    {
        // Half widths 0.3 and 0.45 reach 0.75; 0.3 apart they overlap by 0.45, a share of 0.6.
        const QcVec3 push = QcBodyPush::between(body(0.3, 0.0), body(0.0, 0.0, 0.45));
        QCOMPARE(push.y, 0.0);
        QCOMPARE(push.z, 0.0);
        QVERIFY(qFuzzyCompare(push.x, 0.6));

        // The other body is pushed the opposite way by as much.
        const QcVec3 back = QcBodyPush::between(body(0.0, 0.0, 0.45), body(0.3, 0.0));
        QVERIFY(qFuzzyCompare(back.x, -0.6));

        // Diagonally: along the line between the feet.
        const QcVec3 diagonal = QcBodyPush::between(body(0.1, 0.1), body(0.0, 0.0));
        QVERIFY(qFuzzyCompare(diagonal.x, diagonal.z));
        QVERIFY(diagonal.x > 0.0);
    }

    void noPushWithoutOverlap()
    {
        // Just touching.
        QCOMPARE(QcBodyPush::between(body(0.6, 0.0), body(0.0, 0.0)), QcVec3{});
        QCOMPARE(QcBodyPush::between(body(0.0, 2.0), body(0.0, 0.0)), QcVec3{});
        // One standing on a block above the other's head: their heights do not overlap.
        QCOMPARE(QcBodyPush::between(body(0.1, 0.0, 0.3, 66.0), body(0.0, 0.0, 0.3, 64.0, 1.4)), QcVec3{});
        // A child below a tall one's feet level still overlaps where the heights meet.
        QVERIFY(QcBodyPush::between(body(0.1, 0.0, 0.3, 65.0), body(0.0, 0.0, 0.3, 64.0, 1.4)).x > 0.0);
    }

    void onTheSameSpotPlayerAndCreatureGoOppositeWays()
    {
        const QcVec3 player = QcBodyPush::between(body(0.0, 0.0), body(0.0, 0.0));
        const QcVec3 creature = QcBodyPush::between(body(0.0, 0.0), body(0.0, 0.0), QcBodyPush::creatureParting);
        QCOMPARE(player, (QcVec3{1.0, 0.0, 0.0}));
        QCOMPARE(creature, (QcVec3{-1.0, 0.0, 0.0}));
    }

    void thePlayersPushIsNoFasterThanItsSpeed()
    {
        QCOMPARE(QcBodyPush::playerVelocity({0.5, 0.0, 0.0}), (QcVec3{0.5 * QcBodyPush::playerSpeed, 0.0, 0.0}));
        const QcVec3 crowd = QcBodyPush::playerVelocity({3.0, 0.0, 4.0});
        QVERIFY(qFuzzyCompare(crowd.length(), QcBodyPush::playerSpeed));
        QVERIFY(qFuzzyCompare(crowd.x / crowd.z, 0.75));
        // Slower than walking: a walking player always gets through.
        QVERIFY(QcBodyPush::playerSpeed < QcPhysics::walkSpeed);
    }

    void aWalkerGoesAroundABodyInItsWay()
    {
        // With the wish: unchanged.
        QCOMPARE(QcBodyPush::passing({0.5, 0.0, 0.0}, {1.0, 0.0, 0.0}), (QcVec3{0.5, 0.0, 0.0}));
        // Straight against it: the sideways share to the left of the push is added, so the walker
        // does not stall where wish and push cancel.
        const QcVec3 head = QcBodyPush::passing({-0.5, 0.0, 0.0}, {1.0, 0.0, 0.0});
        QCOMPARE(head, (QcVec3{-0.5, 0.0, -0.5}));
        // Obliquely against it: turned towards the side the wish already leans to.
        const QcVec3 oblique = QcBodyPush::passing({-0.5, 0.0, 0.0}, QcVec3{1.0, 0.0, 0.3}.normalized());
        QVERIFY(oblique.z > 0.0);
        QCOMPARE(oblique.x, -0.5);
    }

    void theGridSumsLikeAllPairs()
    {
        // Bodies of all sizes around the origin, also on negative and column-edge coordinates, and
        // one wide one that reaches over several columns.
        QRandomGenerator random(42);
        QcBodyGrid grid;
        QList<QcBodyPush::Body> bodies;
        for (int i = 0; i < 400; ++i) {
            const double halfWidth = i == 0 ? 1.5 : 0.1 + random.generateDouble() * 0.6;
            const QcBodyPush::Body one = body(random.generateDouble() * 16.0 - 8.0, std::floor(random.generateDouble() * 16.0 - 8.0), halfWidth,
                                              64.0 + std::floor(random.generateDouble() * 2.0), 0.5 + random.generateDouble() * 1.5);
            bodies.append(one);
            grid.add(one);
        }
        grid.finish();
        QCOMPARE(grid.size(), 400);

        int pushed = 0;
        for (int i = 0; i < 300; ++i) {
            const QcBodyPush::Body self = body(random.generateDouble() * 16.0 - 8.0, random.generateDouble() * 16.0 - 8.0);
            QcVec3 expected;
            for (const QcBodyPush::Body &other : std::as_const(bodies)) {
                expected += QcBodyPush::between(self, other);
            }
            const QcVec3 sum = grid.sum(self);
            QVERIFY2((sum - expected).length() < 1e-12, qPrintable(QStringLiteral("%1 %2").arg(sum.x).arg(expected.x)));
            pushed += expected.length() > 0.0 ? 1 : 0;
        }
        // The check means something: many of the probes overlap a body.
        QVERIFY2(pushed > 100, qPrintable(QString::number(pushed)));

        grid.clear();
        grid.finish();
        QCOMPARE(grid.sum(body(0.0, 0.0)), QcVec3{});
    }

    void aPushMovesThisTickOnlyAndStopsAtWalls()
    {
        QcTestWorld world(m_content);
        world.fillBox({-5, 63, -5}, {5, 63, 5}, stone);
        world.fillBox({2, 64, -5}, {2, 66, 5}, stone);
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        motion.onGround = true;

        QcPlayerInput input;
        input.push = {2.0, 0.0, 0.0};
        motion.stepPlayer(input, world, dt, QcMovement::Walk);
        QVERIFY(qFuzzyCompare(motion.position.x, 0.5 + 2.0 * dt));
        // Not kept: the next tick without a push stands.
        QCOMPARE(motion.velocity.x, 0.0);
        const double pushedTo = motion.position.x;
        motion.stepPlayer({}, world, dt, QcMovement::Walk);
        QCOMPARE(motion.position.x, pushedTo);

        // The wall at x 2 holds the body.
        for (int i = 0; i < 40; ++i) {
            motion.stepPlayer(input, world, dt, QcMovement::Walk);
        }
        QVERIFY2(std::abs(motion.position.x - (2.0 - QcPhysics::playerWidth * 0.5)) < 1e-9, qPrintable(QString::number(motion.position.x)));
    }

    void onIceThePushIsNotAmplified()
    {
        // On ice a velocity kept in the body would add up tick by tick and slide on afterwards.
        QcTestWorld world(*m_base);
        world.fillBox({-5, 63, -5}, {20, 63, 5}, block(u"qtcraft:ice"_s));
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        motion.onGround = true;

        QcPlayerInput input;
        input.push = {2.0, 0.0, 0.0};
        for (int i = 0; i < 20; ++i) {
            const double before = motion.position.x;
            motion.stepPlayer(input, world, dt, QcMovement::Walk);
            QVERIFY2(std::abs(motion.position.x - before - 2.0 * dt) < 1e-9, qPrintable(QString::number(motion.position.x - before)));
        }
        const double pushedTo = motion.position.x;
        for (int i = 0; i < 10; ++i) {
            motion.stepPlayer({}, world, dt, QcMovement::Walk);
        }
        QVERIFY2(std::abs(motion.position.x - pushedTo) < 1e-9, qPrintable(QString::number(motion.position.x - pushedTo)));
    }

    void aPushedBodyStepsUpOntoASlab()
    {
        // The step up takes the whole move of the tick, the push included.
        QcTestWorld world(*m_base);
        world.fillBox({-5, 63, -5}, {5, 63, 5}, block(u"qtcraft:stone"_s));
        world.fillBox({2, 64, -5}, {5, 64, 5}, block(u"qtcraft:oak_slab"_s));
        QcEntityMotion motion;
        motion.position = {0.5, 64.0, 0.5};
        motion.onGround = true;

        QcPlayerInput input;
        input.push = {QcBodyPush::playerSpeed, 0.0, 0.0};
        for (int i = 0; i < 20; ++i) {
            motion.stepPlayer(input, world, dt, QcMovement::Walk);
        }
        QVERIFY2(motion.position.x > 2.5, qPrintable(QString::number(motion.position.x)));
        QVERIFY2(std::abs(motion.position.y - 64.5) < 1e-9, qPrintable(QString::number(motion.position.y)));
    }

    void aSpectatorIsNotPushedAndAFlyerIs()
    {
        QcTestWorld world(m_content);
        QcPlayerInput input;
        input.push = {2.0, 0.0, 0.0};

        QcEntityMotion spectator;
        spectator.position = {0.5, 70.0, 0.5};
        spectator.stepPlayer(input, world, dt, QcMovement::NoClip);
        QCOMPARE(spectator.position.x, 0.5);

        // Creative flight is pushed like walking (decision E-882).
        QcEntityMotion flyer;
        flyer.position = {0.5, 70.0, 0.5};
        input.flying = true;
        flyer.stepPlayer(input, world, dt, QcMovement::MayFly);
        QVERIFY(qFuzzyCompare(flyer.position.x, 0.5 + 2.0 * dt));
    }
};

QTEST_GUILESS_MAIN(TestBodyPush)
#include "test_body_push.moc"

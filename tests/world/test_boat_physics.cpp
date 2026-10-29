// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBoatBody.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A lake three blocks deep (y 60 to 62) on stone; its surface lies at 62.875.
constexpr int lakeTop = 62;
constexpr double surface = lakeTop + 1.0 - QcBoatMotion::liquidSurfaceDrop;
constexpr double floating = surface - QcBoatMotion::draft;

double speedOf(const QcBoatState &boat)
{
    return std::hypot(boat.motion.velocity.x, boat.motion.velocity.z);
}

void run(QcBoatState &boat, const std::optional<QcBoatControls> &controls, const QcBlockAccess &world, int ticks)
{
    for (int i = 0; i < ticks; ++i) {
        boat.step(controls, world);
    }
}

} // namespace

// The shared boat physics (decision E-920), ticked by hand on the real content's water and ice: a
// boat floats on the surface, keeps its speed and turn on water like the role model (forward reaches
// 8 m/s, turning 10 degrees per tick), crawls on grass and races on ice, never rides up a
// waterfall, falls where there is no water and rises when it is under water.
class TestBoatPhysics : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    QcBlockId block(const QString &name) const
    {
        const std::optional<QcBlockId> id = m_content->stateId(u"qtcraft:"_s + name);
        if (!id) {
            QcTestUtil::failCase(u"unknown block %1"_s.arg(name));
            return qcAirBlockId;
        }
        return *id;
    }

    // A lake from x -20 to 200 and z -20 to 20, three blocks deep on stone.
    QcTestWorld lake() const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-21, 59, -21}, {201, 59, 21}, block(u"stone"_s));
        world.fillBox({-20, 60, -20}, {200, lakeTop, 20}, block(u"water"_s));
        return world;
    }

    // A floor of `name` from x -10 to 400 on stone, the boat's feet at y 65.
    QcTestWorld floor(const QString &name) const
    {
        QcTestWorld world(*m_content);
        world.fillBox({-10, 63, -4}, {400, 63, 4}, block(u"stone"_s));
        world.fillBox({-10, 64, -4}, {400, 64, 4}, block(name));
        return world;
    }

    static QcBoatState boatAt(const QcVec3 &feet, float yaw)
    {
        QcBoatState boat;
        boat.motion.position = feet;
        boat.yaw = yaw;
        return boat;
    }

private slots:
    void floatsOnTheSurfaceAndSettles()
    {
        const QcTestWorld world = lake();
        QcBoatState boat = boatAt({0.5, 66.0, 0.5}, 0.0f);
        run(boat, std::nullopt, world, 80);
        QVERIFY2(std::abs(boat.motion.position.y - floating) < 0.01, qPrintable(QString::number(boat.motion.position.y)));
        QVERIFY(boat.motion.inWater);
        QVERIFY(!boat.motion.onGround);
        QVERIFY(std::abs(boat.motion.velocity.y) < 0.05);
        // The floor stays above the water.
        QVERIFY(boat.motion.position.y + QcBoatBody::floor > surface);

        // Under water it rises slowly to the surface (at most QcBoatMotion::floatSpeed).
        QcBoatState sunk = boatAt({0.5, 60.0, 0.5}, 0.0f);
        double highest = sunk.motion.position.y;
        for (int i = 0; i < 60; ++i) {
            const double before = sunk.motion.position.y;
            sunk.step(std::nullopt, world);
            QVERIFY(sunk.motion.position.y - before <= QcBoatMotion::floatSpeed * QcPhysics::tickDuration + 1e-9);
            highest = std::max(highest, sunk.motion.position.y);
        }
        QVERIFY2(std::abs(sunk.motion.position.y - floating) < 0.01, qPrintable(QString::number(sunk.motion.position.y)));
        QVERIFY(highest < floating + 0.05);
    }

    void paddlesWithInertiaLikeTheRoleModel()
    {
        const QcTestWorld world = lake();
        // Pointing east (yaw 90) the boat paddles along +x.
        QcBoatState boat = boatAt({0.5, floating, 0.5}, 90.0f);
        run(boat, QcBoatControls{1.0, 0.0}, world, 60);
        const double top = QcBoatMotion::paddleForward / (1.0 - QcBoatMotion::waterKeep);
        QVERIFY2(std::abs(speedOf(boat) - top) < 0.05, qPrintable(QString::number(speedOf(boat))));
        QVERIFY(boat.motion.velocity.x > 0.0);
        QVERIFY(std::abs(boat.motion.velocity.z) < 1e-9);
        QVERIFY(std::abs(boat.motion.position.y - floating) < 0.01);

        // Let go, it glides on and slows down: 90 % of the speed per tick.
        const double gliding = speedOf(boat);
        const double from = boat.motion.position.x;
        run(boat, QcBoatControls{}, world, 1);
        QVERIFY(std::abs(speedOf(boat) - gliding * 0.9) < 1e-6);
        run(boat, QcBoatControls{}, world, 60);
        QVERIFY(speedOf(boat) < 0.02);
        QVERIFY2(boat.motion.position.x - from > 3.0, qPrintable(QString::number(boat.motion.position.x - from)));

        // The back key brakes and paddles back slowly.
        QcBoatState back = boatAt({0.5, floating, 0.5}, 90.0f);
        run(back, QcBoatControls{-1.0, 0.0}, world, 60);
        QVERIFY(back.motion.velocity.x < 0.0);
        QVERIFY(speedOf(back) <= QcBoatMotion::paddleBack / (1.0 - QcBoatMotion::waterKeep) + 1e-9);
    }

    void turnsWithInertia()
    {
        const QcTestWorld world = lake();
        QcBoatState boat = boatAt({0.5, floating, 0.5}, 0.0f);
        // Right turns the yaw up (towards east), at most 10 degrees per tick on water.
        run(boat, QcBoatControls{0.0, 1.0}, world, 5);
        QVERIFY(boat.yaw > 0.0f && boat.yaw < 90.0f);
        run(boat, QcBoatControls{0.0, 1.0}, world, 60);
        QVERIFY2(std::abs(boat.spin - 10.0f) < 0.05f, qPrintable(QString::number(boat.spin)));
        // Turning on the spot drifts it forward a little, never fast.
        QVERIFY(speedOf(boat) < 1.0);

        // Let go, the turn dies away; left turns the other way.
        run(boat, QcBoatControls{}, world, 60);
        QVERIFY(std::abs(boat.spin) < 0.05f);
        QcBoatState left = boatAt({0.5, floating, 0.5}, 180.0f);
        run(left, QcBoatControls{0.0, -1.0}, world, 5);
        QVERIFY(left.yaw < 180.0f && left.yaw > 90.0f);

        // Paddling while turning goes round in a circle and comes back near its start.
        QcBoatState round = boatAt({0.5, floating, 0.5}, 0.0f);
        double farthest = 0.0;
        for (int i = 0; i < 400; ++i) {
            round.step(QcBoatControls{1.0, 1.0}, world);
            farthest = std::max(farthest, std::hypot(round.motion.position.x - 0.5, round.motion.position.z - 0.5));
        }
        QVERIFY2(farthest < 16.0, qPrintable(QString::number(farthest)));
    }

    void crawlsOnGrassAndRacesOnIce()
    {
        // On ground that grips it keeps 60 % per tick: it reaches 2 m/s and turns slowly.
        const QcTestWorld grass = floor(u"grass_block"_s);
        QcBoatState boat = boatAt({0.5, 65.0, 0.5}, 90.0f);
        run(boat, QcBoatControls{1.0, 0.0}, grass, 60);
        QVERIFY(boat.motion.onGround);
        QVERIFY(!boat.motion.inWater);
        QVERIFY2(std::abs(speedOf(boat) - 2.0) < 0.01, qPrintable(QString::number(speedOf(boat))));
        QcBoatState turning = boatAt({0.5, 65.0, 0.5}, 0.0f);
        run(turning, QcBoatControls{0.0, 1.0}, grass, 60);
        QVERIFY2(turning.spin < 2.6f, qPrintable(QString::number(turning.spin)));

        // On packed ice it keeps 98 %: much faster than on water, as in the role model.
        const QcTestWorld ice = floor(u"packed_ice"_s);
        QcBoatState racer = boatAt({0.5, 65.0, 0.5}, 90.0f);
        run(racer, QcBoatControls{1.0, 0.0}, ice, 100);
        QVERIFY2(speedOf(racer) > 25.0, qPrintable(QString::number(speedOf(racer))));
        QVERIFY(speedOf(racer) <= QcBoatMotion::maxSpeed);
    }

    void neverRidesUpAWaterfall()
    {
        // A waterfall falls from a cliff at x 10 into the lake; the boat paddles into it.
        QcTestWorld world = lake();
        world.fillBox({11, 60, -20}, {14, 75, 20}, block(u"stone"_s));
        world.fillBox({10, lakeTop + 1, -3}, {10, 75, 3}, block(u"water[falling=true,level=1]"_s));
        QcBoatState boat = boatAt({0.5, floating, 0.5}, 90.0f);
        double highest = boat.motion.position.y;
        for (int i = 0; i < 200; ++i) {
            boat.step(QcBoatControls{1.0, 0.0}, world);
            highest = std::max(highest, boat.motion.position.y);
        }
        QVERIFY2(highest < floating + 0.01, qPrintable(QString::number(highest)));
        // It stopped at the cliff, its box against it.
        QVERIFY2(std::abs(boat.motion.position.x + QcBoatBody::width * 0.5 - 11.0) < 1e-6, qPrintable(QString::number(boat.motion.position.x)));
        QCOMPARE(boat.motion.velocity.x, 0.0);

        // A boat in the falling water above the lake falls through it into the lake.
        QcBoatState falling = boatAt({10.5 - 0.7, 72.0, 0.5}, 90.0f);
        run(falling, std::nullopt, world, 80);
        QVERIFY2(std::abs(falling.motion.position.y - floating) < 0.05, qPrintable(QString::number(falling.motion.position.y)));
    }

    void fallsWithoutWater()
    {
        // Above stone it falls and lands; a lake drained under a boat lets it down to the bottom.
        QcTestWorld world = lake();
        QcBoatState boat = boatAt({0.5, floating, 0.5}, 0.0f);
        run(boat, std::nullopt, world, 5);
        world.fillBox({-20, 60, -20}, {200, lakeTop, 20}, qcAirBlockId);
        run(boat, std::nullopt, world, 60);
        QCOMPARE(boat.motion.position.y, 60.0);
        QVERIFY(boat.motion.onGround);
        QVERIFY(!boat.motion.inWater);
    }

    void stepsAlikeEveryTime()
    {
        // The step is a pure function of the state, the controls and the world: the server and the
        // driver's client, starting from the same state, end in the same place (no corrections).
        const QcTestWorld world = lake();
        QcBoatState first = boatAt({0.5, floating, 0.5}, 33.0f);
        QcBoatState second = first;
        for (int i = 0; i < 300; ++i) {
            const QcBoatControls controls{(i / 40) % 2 == 0 ? 1.0 : -0.5, (i / 25) % 3 - 1.0};
            first.step(controls, world);
            second.step(controls, world);
        }
        QCOMPARE(first, second);
    }

    void controlsFollowTheKeys()
    {
        QcPlayerInput input;
        input.forward = 1.0f;
        input.strafe = -1.0f;
        QCOMPARE(QcBoatControls::of(input), (QcBoatControls{1.0, -1.0}));
        input.forward = 3.0f;
        input.strafe = std::numeric_limits<float>::quiet_NaN();
        QCOMPARE(QcBoatControls::of(input), (QcBoatControls{1.0, 0.0}));
    }
};

QTEST_GUILESS_MAIN(TestBoatPhysics)
#include "test_boat_physics.moc"

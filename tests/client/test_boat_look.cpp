// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcBoatModel.h"
#include "clientcore/meshing/QcBoatRiders.h"
#include "common/physics/QcBoatMotion.h"
#include "common/protocol/QcEntityMessages.h"
#include "common/world/QcBoatBody.h"

#include <QTest>

#include <cmath>

// How a boat and its driver look (decision E-920): the paddles rest along the sides with their
// blades in the water and row from the bow to the stern, mirror images of each other; the driver
// sits low with level thighs and his arms reach to the grips in every moment of the stroke; other
// players on a boat's driver seat are found and seated in the boat's way.
class TestBoatLook : public QObject
{
    Q_OBJECT

private:
    // The water surface in the boat's frame (its feet lie `draft` below it).
    static constexpr double water = QcBoatMotion::draft;

    static QcVec3 bladeOf(int side, QcBoatModel::Paddle paddle) { return QcBoatModel::paddlePoint(side, paddle, {0.835, 0.0, 0.0}); }

private slots:
    void paddlesRestInTheWaterAlongTheSides()
    {
        const QcBoatModel::Paddle rest = QcBoatModel::paddleAt(-1.0);
        for (const int side : {1, -1}) {
            const QcVec3 blade = bladeOf(side, rest);
            QVERIFY2(blade.y < water, qPrintable(QString::number(blade.y)));
            // Outside the hull, towards the stern (+z).
            QVERIFY(side * blade.x > 0.64);
            QVERIFY(blade.z > 0.3);
            // The grip lies inside the boat, above the side wall.
            const QcVec3 grip = QcBoatModel::gripOf(side, rest);
            QVERIFY(side * grip.x < 0.55);
            QVERIFY(grip.y > QcBoatModel::wallHeight);
        }
        // Mirror images.
        const QcVec3 right = bladeOf(1, rest);
        const QcVec3 left = bladeOf(-1, rest);
        QCOMPARE(left.x, -right.x);
        QCOMPARE(left.y, right.y);
        QCOMPARE(left.z, right.z);
        // A box corner of the left paddle's mirrored parts lands where the mirrored point does.
        const QcVec3 corner = QcBoatModel::partPoint(-1, rest, {-0.835, 0.0, 0.0});
        QVERIFY((corner - left).length() < 1e-12);
    }

    void aStrokeRowsFromTheBowToTheSternWithTheBladeInTheWater()
    {
        // The stroke (first half): the blade in the water and moving towards the stern; the
        // recovery: the blade above the water, back towards the bow.
        double previousZ = bladeOf(1, QcBoatModel::paddleAt(0.0)).z;
        for (int step = 1; step <= 20; ++step) {
            const double phase = step / 20.0;
            const QcVec3 blade = bladeOf(1, QcBoatModel::paddleAt(phase));
            if (phase > 0.1 && phase < 0.4) {
                QVERIFY2(blade.y < water + 0.12, qPrintable(QString::number(blade.y)));
                QVERIFY(blade.z > previousZ);
            } else if (phase > 0.6 && phase < 0.9) {
                QVERIFY2(blade.y > water, qPrintable(QString::number(blade.y)));
                QVERIFY(blade.z < previousZ);
            }
            previousZ = blade.z;
        }
        QCOMPARE(QcBoatModel::rowPhase(1.0, false), -1.0);
        const double phase = QcBoatModel::rowPhase(12.3, true);
        QVERIFY(phase >= 0.0 && phase < 1.0);
    }

    void theDriverSitsLowAndHoldsTheGrips()
    {
        // The hips just above the floor, the thighs level.
        const QcVec3 seat = QcBoatBody::driverSeat({}, 0.0);
        QCOMPARE(seat.y + QcBoatBody::hipHeight, QcBoatBody::floor + QcBoatBody::seatHeight);
        QVERIFY(seat.z > 0.2);

        for (const double phase : {-1.0, 0.0, 0.25, 0.5, 0.75}) {
            QcEntityPose pose;
            pose.inBoat = true;
            pose.rowPhase = phase;
            const QcAnimator::BodyPose body = QcAnimator::animateBody(pose, 0.0);
            QCOMPARE(body.bone(QcBoneRole::RightLeg).pitch, 90.0);
            QCOMPARE(body.bone(QcBoneRole::LeftLeg).pitch, 90.0);
            // The arms reach forward to the grips, the right one to the right, mirrored.
            const QcAnimator::BoneRotation right = body.bone(QcBoneRole::RightArm);
            const QcAnimator::BoneRotation left = body.bone(QcBoneRole::LeftArm);
            QVERIFY2(right.pitch > 30.0 && right.pitch < 120.0, qPrintable(QString::number(right.pitch)));
            QCOMPARE(left.pitch, right.pitch);
            QCOMPARE(left.roll, -right.roll);
        }
        // Rowing moves the arms: reaching further at the end of the recovery than at the end of the
        // stroke.
        QcEntityPose pulled;
        pulled.inBoat = true;
        pulled.rowPhase = 0.5;
        QcEntityPose reaching = pulled;
        reaching.rowPhase = 0.0;
        QVERIFY(QcAnimator::animateBody(pulled, 0.0).bone(QcBoneRole::RightArm).pitch
                != QcAnimator::animateBody(reaching, 0.0).bone(QcBoneRole::RightArm).pitch);
    }

    void playersOnADriverSeatAreSeatedInTheBoatsWay()
    {
        QcClientEntity boat;
        boat.id = 7;
        boat.kind = QcEntityKind::Boat;
        boat.position = boat.previousPosition = {10.5, 63.8, 4.5};
        boat.yaw = boat.previousYaw = 90.0f;
        boat.flags = QcMsg::Rowing;
        QcClientEntity driver;
        driver.id = 8;
        driver.kind = QcEntityKind::Player;
        driver.position = driver.previousPosition = QcBoatBody::driverSeat(boat.position, 90.0);
        driver.flags = QcMsg::Riding;
        QcClientEntity walker = driver;
        walker.id = 9;
        walker.flags = 0;
        QcClientEntity carted = driver;
        carted.id = 10;
        carted.position = carted.previousPosition = boat.position + QcVec3{5.0, 0.0, 0.0};
        const QHash<quint32, QcClientEntity> entities{{7, boat}, {8, driver}, {9, walker}, {10, carted}};

        const QHash<quint32, QcBoatRiders::Seat> seats = QcBoatRiders::seats(entities, 0.5, 3.0);
        QCOMPARE(seats.size(), 1);
        QVERIFY(seats.contains(8));
        QCOMPARE(seats.value(8).boat, 7u);
        QCOMPARE(seats.value(8).yaw, 90.0);
        QVERIFY(seats.value(8).rowPhase >= 0.0);
        QCOMPARE(QcBoatRiders::headYaw(100.0, 90.0), 10.0);
        QCOMPARE(QcBoatRiders::headYaw(-170.0, 170.0), 20.0);
    }
};

QTEST_GUILESS_MAIN(TestBoatLook)
#include "test_boat_look.moc"

// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/figures/QcCapeSwing.h"
#include "clientcore/figures/QcCapeTracker.h"
#include "common/physics/QcPhysics.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

QcCapeDrive moving(double forward, double vertical = 0.0, bool onGround = true)
{
    QcCapeDrive drive;
    drive.forwardSpeed = forward;
    drive.verticalSpeed = vertical;
    drive.onGround = onGround;
    return drive;
}

// Mean target over a few seconds, so the idle sway and the flutter average out.
double meanTarget(const QcCapeDrive &drive)
{
    double sum = 0.0;
    constexpr int samples = 600;
    for (int i = 0; i < samples; ++i) {
        sum += QcCapeSwing::target(drive, i * 0.01).lift;
    }
    return sum / samples;
}

// The cape after `seconds` from rest, advanced in frames of `frame` seconds (a jittered frame
// time when `jitter` is set: alternately 40 % longer and shorter).
QcCapeSwing::Angles simulate(const QcCapeDrive &drive, double seconds, double frame, bool jitter = false)
{
    QcCapeSwing swing;
    swing.settle(QcCapeDrive{}, 0.0);
    double time = 0.0;
    int index = 0;
    while (time < seconds - 1e-12) {
        double dt = jitter ? frame * (index % 2 == 0 ? 1.4 : 0.6) : frame;
        dt = std::min(dt, seconds - time);
        time += dt;
        swing.advance(drive, dt, time);
        ++index;
    }
    return swing.angles();
}

} // namespace

// The cape's swing (decision E-240): targets from the movement, a damped spring towards them that
// does not depend on the frame rate, and its limits. GUI-free.
class TestCapeSwing : public QObject
{
    Q_OBJECT

private slots:
    void targetsFollowTheMovement()
    {
        const double standing = meanTarget({});
        const double walking = meanTarget(moving(QcPhysics::walkSpeed));
        const double sprinting = meanTarget(moving(QcPhysics::sprintSpeed));
        const double falling = meanTarget(moving(0.0, -12.0, false));
        const double rising = meanTarget(moving(0.0, QcPhysics::jumpVelocity, false));
        const double backwards = meanTarget(moving(-QcPhysics::walkSpeed));

        // Standing: close to the back, swaying by a few degrees around the rest angle.
        QVERIFY(std::abs(standing - QcCapeSwing::restLift) < 1.0);
        for (int step = 0; step < 200; ++step) {
            QVERIFY(std::abs(QcCapeSwing::target({}, step * 0.05).lift - QcCapeSwing::restLift) <= 4.0);
        }
        // Walking lifts it clearly, sprinting more, falling most (upwards past horizontal).
        QVERIFY2(walking > standing + 25.0, qPrintable(QString::number(walking)));
        QVERIFY2(sprinting > walking + 15.0, qPrintable(QString::number(sprinting)));
        QVERIFY2(falling > 80.0, qPrintable(QString::number(falling)));
        // Rising after a jump presses it to the back; walking backwards holds it off the legs that
        // swing back into it (decision E-240), but far less than walking forwards.
        QVERIFY(rising < standing);
        QVERIFY2(backwards > standing + 10.0 && backwards < walking - 10.0, qPrintable(QString::number(backwards)));
        for (int step = 0; step < 200; ++step) {
            QVERIFY(QcCapeSwing::target(moving(-QcPhysics::walkSpeed), step * 0.05).lift >= 20.0);
        }

        // Sneaking: flat on the back, no sway; in water it only trails a little.
        QcCapeDrive sneaking = moving(QcPhysics::sneakSpeed);
        sneaking.sneaking = true;
        QCOMPARE(QcCapeSwing::target(sneaking, 1.3).lift, QcCapeSwing::minLift);
        QCOMPARE(QcCapeSwing::target(sneaking, 1.3).sway, 0.0);
        QcCapeDrive swimming = moving(QcPhysics::swimSpeed, 0.0, false);
        swimming.inWater = true;
        QVERIFY(meanTarget(swimming) < walking);

        // Stepping to the right swings it to the left (positive sway), and the other way round.
        QcCapeDrive right;
        right.sideSpeed = 3.0;
        QcCapeDrive left;
        left.sideSpeed = -3.0;
        QVERIFY(QcCapeSwing::target(right, 0.0).sway > 10.0);
        QVERIFY(QcCapeSwing::target(left, 0.0).sway < -10.0);
    }

    void targetsStayWithinTheLimits()
    {
        for (const double forward : {-60.0, -5.0, 0.0, 5.0, 60.0}) {
            for (const double vertical : {-60.0, -10.0, 0.0, 10.0, 60.0}) {
                for (const double side : {-30.0, 0.0, 30.0}) {
                    QcCapeDrive drive = moving(forward, vertical, vertical == 0.0);
                    drive.sideSpeed = side;
                    for (int step = 0; step < 9; ++step) {
                        const QcCapeSwing::Angles target = QcCapeSwing::target(drive, step * 0.37);
                        QVERIFY(target.lift >= QcCapeSwing::minLift && target.lift <= QcCapeSwing::maxLift);
                        QVERIFY(std::abs(target.sway) <= QcCapeSwing::maxSway);
                    }
                }
            }
        }
    }

    void springIsDampedAndBounded()
    {
        // From rest to sprinting: the cape rises over a few tenths of a second, overshoots the mean
        // target by little and settles near it.
        const QcCapeDrive sprint = moving(QcPhysics::sprintSpeed);
        const double goal = meanTarget(sprint);
        const double early = simulate(sprint, 0.1, 1.0 / 60.0).lift;
        QVERIFY2(early > QcCapeSwing::restLift && early < goal * 0.6, qPrintable(QString::number(early)));

        QcCapeSwing swing;
        swing.settle({}, 0.0);
        double highest = 0.0;
        double time = 0.0;
        for (int frame = 0; frame < 6 * 60; ++frame) {
            time += 1.0 / 60.0;
            swing.advance(sprint, 1.0 / 60.0, time);
            highest = std::max(highest, swing.angles().lift);
        }
        QVERIFY2(highest < goal * 1.35, qPrintable(u"%1 over %2"_s.arg(highest).arg(goal)));
        QVERIFY2(std::abs(swing.angles().lift - goal) < 6.0, qPrintable(QString::number(swing.angles().lift)));

        // A violent change never leaves the limits.
        QcCapeSwing wild;
        time = 0.0;
        for (int frame = 0; frame < 600; ++frame) {
            time += 1.0 / 30.0;
            const bool up = (frame / 7) % 2 == 0;
            QcCapeDrive drive = moving(up ? 60.0 : -60.0, up ? -60.0 : 60.0, !up);
            drive.sideSpeed = up ? 40.0 : -40.0;
            wild.advance(drive, 1.0 / 30.0, time);
            QVERIFY(wild.angles().lift >= QcCapeSwing::minLift && wild.angles().lift <= QcCapeSwing::maxLift);
            QVERIFY(std::abs(wild.angles().sway) <= QcCapeSwing::maxSway);
        }

        // A long pause (a hidden window) moves it by at most maxAdvance, not all at once.
        QcCapeSwing paused;
        paused.settle({}, 0.0);
        paused.advance(moving(0.0, -12.0, false), 30.0, 30.0);
        const double afterPause = paused.angles().lift;
        QcCapeSwing reference;
        reference.settle({}, 0.0);
        reference.advance(moving(0.0, -12.0, false), QcCapeSwing::maxAdvance, 30.0);
        QCOMPARE(afterPause, reference.angles().lift);
        QVERIFY(afterPause < meanTarget(moving(0.0, -12.0, false)));
    }

    void independentOfTheFrameRate()
    {
        // The same movement drawn at 30, 60, 144 and 240 frames per second, and with uneven frame
        // times, gives the same cape: during the rise and after it settled.
        const QcCapeDrive walk = moving(QcPhysics::walkSpeed);
        for (const double seconds : {0.35, 3.0}) {
            const QcCapeSwing::Angles reference = simulate(walk, seconds, 1.0 / 60.0);
            for (const double frame : {1.0 / 30.0, 1.0 / 144.0, 1.0 / 240.0}) {
                for (const bool jitter : {false, true}) {
                    const QcCapeSwing::Angles angles = simulate(walk, seconds, frame, jitter);
                    QVERIFY2(std::abs(angles.lift - reference.lift) < 1.0,
                             qPrintable(u"%1 s at %2 fps: %3 against %4"_s.arg(seconds).arg(1.0 / frame).arg(angles.lift).arg(reference.lift)));
                    QVERIFY(std::abs(angles.sway - reference.sway) < 1.0);
                }
            }
        }
    }

    void driveComesFromTheVelocityInTheFiguresFrame()
    {
        QcEntityPose pose;
        pose.onGround = false;
        pose.sneaking = true;
        // Yaw 0 faces -Z: moving towards -Z is forwards, towards +X to the right.
        QcCapeDrive drive = QcCapeDrive::from({1.0, -2.0, -4.0}, 0.0, pose);
        QVERIFY(std::abs(drive.forwardSpeed - 4.0) < 1e-9);
        QVERIFY(std::abs(drive.sideSpeed - 1.0) < 1e-9);
        QCOMPARE(drive.verticalSpeed, -2.0);
        QVERIFY(!drive.onGround);
        QVERIFY(drive.sneaking);
        // Yaw 90 faces +X.
        drive = QcCapeDrive::from({3.0, 0.0, 0.5}, 90.0, pose);
        QVERIFY(std::abs(drive.forwardSpeed - 3.0) < 1e-9);
        QVERIFY(std::abs(drive.sideSpeed - 0.5) < 1e-9);
    }

    void trackerKeepsOneSwingPerDrawnFigure()
    {
        QcCapeTracker tracker;
        const QcCapeDrive walk = moving(QcPhysics::walkSpeed);
        // First sight: at rest on its target.
        tracker.beginFrame(0.0, 1.0);
        QCOMPARE(tracker.advance(7, walk).lift, QcCapeSwing::target(walk, 1.0).lift);
        tracker.advance(QcCapeTracker::localPlayer, {});
        tracker.endFrame();
        QCOMPARE(tracker.size(), 2);

        // Figure 7 moves on with the frame's time; a figure not drawn loses its swing.
        tracker.beginFrame(0.5, 1.5);
        const double moved = tracker.advance(7, moving(QcPhysics::sprintSpeed)).lift;
        QVERIFY(moved > QcCapeSwing::target(walk, 1.0).lift);
        tracker.endFrame();
        QCOMPARE(tracker.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestCapeSwing)
#include "test_cape_swing.moc"

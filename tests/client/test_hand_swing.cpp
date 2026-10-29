// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcHandSwing.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "clientcore/player/QcPrediction.h"
#include "common/gameplay/QcSwingSpeed.h"

#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr QcHandSwing::Style styles[] = {QcHandSwing::Style::Mining, QcHandSwing::Style::Click};

double pitchAt(double progress, QcHandSwing::Style style)
{
    return QcHandSwing::at(progress, style).pitch;
}

// The swing progress of the local player after the last tick, at the start of the tick.
double progressOf(const QcLocalAnimation &animation)
{
    const QcPrediction prediction;
    return animation.localPose(0.0, prediction, -1.0).swingProgress;
}

} // namespace

// The first-person swing and its length (decision E-881): the shape of the rigid swing, the
// seamless mining loop and the swing length from status effects.
class TestHandSwing : public QObject
{
    Q_OBJECT

private slots:
    void swingStartsAndEndsAtRest()
    {
        for (const QcHandSwing::Style style : styles) {
            QCOMPARE(pitchAt(0.0, style), 0.0);
            QCOMPARE(pitchAt(1.0, style), 0.0);
            QCOMPARE(QcHandSwing::at(1.0, style).yaw, 0.0);
            // Out of range is clamped.
            QCOMPARE(pitchAt(-0.5, style), 0.0);
            QCOMPARE(pitchAt(1.5, style), 0.0);
        }
    }

    // Mining repeats the swing: where one ends and the next begins neither the angle nor its speed
    // jumps (both ends at rest without speed), and nowhere inside does the angle jump.
    void loopIsSeamless()
    {
        constexpr double step = 1e-4;
        for (const QcHandSwing::Style style : styles) {
            const double speedAtEnd = (pitchAt(1.0, style) - pitchAt(1.0 - step, style)) / step;
            const double speedAtStart = (pitchAt(step, style) - pitchAt(0.0, style)) / step;
            QVERIFY2(std::abs(speedAtEnd) < 1.0 && std::abs(speedAtStart) < 1.0, qPrintable(u"%1 %2"_s.arg(speedAtEnd).arg(speedAtStart)));

            double previous = pitchAt(0.0, style);
            for (int i = 1; i <= 10000; ++i) {
                const double pitch = pitchAt(i * step, style);
                QVERIFY2(std::abs(pitch - previous) < 0.1, qPrintable(QString::number(i * step)));
                previous = pitch;
            }
        }
    }

    // Up to the highest point, then the strike gains speed down to the lowest point (ease-in), a
    // short recoil and back; the strike turns the arm inwards.
    void swingLiftsStrikesAndRecoils()
    {
        for (const QcHandSwing::Style style : styles) {
            const QcHandSwing::Shape &shape = QcHandSwing::shapeOf(style);
            QVERIFY(0.0 < shape.windUpEnd && shape.windUpEnd < shape.strikeEnd && shape.strikeEnd < shape.recoilEnd && shape.recoilEnd < 1.0);
            QCOMPARE(pitchAt(shape.windUpEnd, style), shape.lift);
            QCOMPARE(pitchAt(shape.strikeEnd, style), shape.strike);
            QCOMPARE(pitchAt(shape.recoilEnd, style), shape.recoil);
            QVERIFY(shape.lift > 0.0 && shape.strike < shape.recoil && shape.recoil < 0.0);

            // Ease-in: the second half of the strike covers more than the first.
            const double middle = (shape.windUpEnd + shape.strikeEnd) / 2.0;
            QVERIFY(pitchAt(shape.windUpEnd, style) - pitchAt(middle, style) < pitchAt(middle, style) - pitchAt(shape.strikeEnd, style));
            // Monotonic in each phase.
            for (int i = 1; i < 10; ++i) {
                const double t = shape.windUpEnd + (shape.strikeEnd - shape.windUpEnd) * i / 10.0;
                QVERIFY(pitchAt(t, style) < pitchAt(t - 0.001, style));
            }
            QVERIFY(QcHandSwing::at(shape.strikeEnd, style).yaw > 3.0);
        }

        // A click is the short swing: flatter and sooner than mining's.
        const QcHandSwing::Shape &mining = QcHandSwing::shapeOf(QcHandSwing::Style::Mining);
        const QcHandSwing::Shape &click = QcHandSwing::shapeOf(QcHandSwing::Style::Click);
        QVERIFY(click.lift - click.strike < mining.lift - mining.strike);
        QVERIFY(click.strikeEnd < mining.strikeEnd);
    }

    // The reference's lengths: six ticks, one less per level of haste, two more per level of mining
    // fatigue, within the bounds.
    void swingLengthFollowsEffects()
    {
        QCOMPARE(QcSwingSpeed{}.ticks(), 6);
        QCOMPARE(QcSwingSpeed{}.ticks(), QcSwingSpeed::baseTicks);
        QCOMPARE((QcSwingSpeed{.faster = 2}).ticks(), 4);
        QCOMPARE((QcSwingSpeed{.slower = 1}).ticks(), 8);
        QCOMPARE((QcSwingSpeed{.faster = 1, .slower = 1, .extraTicks = -1}).ticks(), 6);
        QCOMPARE((QcSwingSpeed{.faster = 50}).ticks(), QcSwingSpeed::minTicks);
        QCOMPARE((QcSwingSpeed{.slower = 1'000'000'000}).ticks(), QcSwingSpeed::maxTicks);
        QCOMPARE((QcSwingSpeed{.faster = -3}).ticks(), 6);

        const QList<QcStatusEffect> effects{{u"haste"_s, 2, 100}, {u"slowness"_s, 3, 100}};
        QCOMPARE(QcSwingSpeed::of(effects), (QcSwingSpeed{.faster = 2}));
        QCOMPARE(QcSwingSpeed::of({{u"mining_fatigue"_s, 1, 100}}).ticks(), 8);
        // Effects are named with their pack's namespace; any pack's haste counts.
        QCOMPARE(QcSwingSpeed::of({{u"mypack:haste"_s, 1, 100}, {u"qtcraft:mining_fatigue"_s, 2, 100}}).ticks(), 9);
        QCOMPARE(QcSwingSpeed::of({{u"mypack:hasted"_s, 1, 100}}).ticks(), QcSwingSpeed::baseTicks);
        QCOMPARE(QcSwingSpeed::of({}).ticks(), QcSwingSpeed::baseTicks);
    }

    // While mining, a swing follows the last in the very tick it ends: the loop's period is the
    // swing's length; a new length applies from the next swing on.
    void miningLoopsWithTheSwingLength()
    {
        QcLocalAnimation animation;
        QCOMPARE(progressOf(animation), -1.0);
        const QcSwingSpeed slow{.slower = 1};

        animation.tick(qcNoItem, true, slow);
        QCOMPARE(progressOf(animation), 0.0);
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int tick = 1; tick < slow.ticks(); ++tick) {
                animation.tick(qcNoItem, true, slow);
                QCOMPARE(progressOf(animation), static_cast<double>(tick) / slow.ticks());
            }
            animation.tick(qcNoItem, true, slow);
            QCOMPARE(progressOf(animation), 0.0);
        }

        // Faster from now on: the running swing keeps its length, the next is shorter.
        const QcSwingSpeed fast{.faster = 2};
        animation.tick(qcNoItem, true, fast);
        QCOMPARE(progressOf(animation), 1.0 / slow.ticks());
        for (int tick = 2; tick < slow.ticks(); ++tick) {
            animation.tick(qcNoItem, true, fast);
        }
        animation.tick(qcNoItem, true, fast);
        QCOMPARE(progressOf(animation), 0.0);
        animation.tick(qcNoItem, true, fast);
        QCOMPARE(progressOf(animation), 1.0 / fast.ticks());
        QVERIFY(animation.handTimers(0.0).mining);

        // Letting go finishes the swing and stops.
        for (int tick = 0; tick < fast.ticks(); ++tick) {
            animation.tick(qcNoItem, false, fast);
        }
        QCOMPARE(progressOf(animation), -1.0);
        QVERIFY(!animation.handTimers(0.0).mining);
    }

    // A swing keeps the style it started with: when a block breaks, mining pauses for one tick
    // (the next block is not hit yet), and the running mining swing must not switch to a click's
    // curve and back; the hand's pitch runs on without a jump. A click's swing stays a click's.
    void swingKeepsItsStyle()
    {
        QcLocalAnimation animation;
        const QcPrediction prediction;
        const auto handPitch = [&](double alpha) {
            return QcAnimator::animateHand(animation.localPose(alpha, prediction, -1.0), 0.0, animation.handTimers(alpha)).pitch;
        };
        const auto mining = [](double progress) { return QcHandSwing::at(progress, QcHandSwing::Style::Mining).pitch; };
        constexpr double length = QcSwingSpeed::baseTicks;

        for (int tick = 0; tick < 3; ++tick) {
            animation.tick(qcNoItem, true, {});
        }
        QCOMPARE(progressOf(animation), 2.0 / length);
        // The last frame of the tick before the block breaks, at progress 0.5.
        const double beforeBreak = handPitch(1.0);

        // The block broke: one tick without mining, then the next block.
        animation.tick(qcNoItem, false, {});
        QVERIFY(animation.handTimers(0.0).mining);
        QCOMPARE(handPitch(0.0), beforeBreak);
        QVERIFY(qFuzzyCompare(handPitch(0.5) - handPitch(0.0), mining(3.5 / length) - mining(3.0 / length)));
        animation.tick(qcNoItem, true, {});
        QCOMPARE(progressOf(animation), 4.0 / length);
        QVERIFY(animation.handTimers(0.0).mining);

        // A click while idle is a click's swing to its end, even if mining starts meanwhile.
        QcLocalAnimation clicked;
        clicked.swing();
        QVERIFY(!clicked.handTimers(0.0).mining);
        clicked.tick(qcNoItem, true, {});
        QVERIFY(!clicked.handTimers(0.0).mining);
    }
};

QTEST_GUILESS_MAIN(TestHandSwing)
#include "test_hand_swing.moc"

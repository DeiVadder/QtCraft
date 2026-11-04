// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// How a bow is held and drawn (decision E-984): the arms of an archer meet in front of the chest,
// the bow stands upright with its string towards the face and its wood towards the target, the
// first-person hand comes to the drawn placement smoothly and the view narrows with the draw.

#include "clientcore/camera/QcFovEffect.h"
#include "clientcore/entitymeshes/QcMeshRotation.h"
#include "clientcore/figures/QcAnimator.h"
#include "clientcore/figures/QcFigureLook.h"
#include "clientcore/player/QcLocalAnimation.h"
#include "common/content/loader/QcBaseContent.h"
#include "common/content/registry/QcContentRegistry.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;
using QcEntityMesher::Rotate;
using QcEntityMesher::Turn;

namespace {

// The bow's icon (QcTextureRangedPatterns): the string runs along the diagonal from the lower left
// to the upper right, the wood bulges towards the upper left. In the sprite's units (x right,
// y up, z out of the picture).
constexpr double diagonal = std::numbers::sqrt2 / 2.0;
const QcVec3 bowAxis{diagonal, diagonal, 0.0};
const QcVec3 bowBelly{-diagonal, diagonal, 0.0};

// A direction of the sprite turned by a held placement, as HeldItemPainter turns its points.
QcVec3 placed(const QcEntityMesher::HeldPlacement &placement, const QcVec3 &direction)
{
    return Rotate(placement.rotate.x, placement.rotate.z)(Turn(placement.rotate.y)(direction));
}

// A direction of an arm's space turned by the arm's rotation in a body pose.
QcVec3 turnedBy(const QcAnimator::BoneRotation &arm, const QcVec3 &direction)
{
    return Rotate(arm.pitch, arm.roll, arm.yaw)(direction);
}

// Where the fist of an arm 0.6 m long hanging from `shoulder` ends up.
QcVec3 fist(const QcAnimator::BoneRotation &arm, const QcVec3 &shoulder)
{
    return shoulder + turnedBy(arm, {0.0, -0.6, 0.0});
}

QString text(const QcVec3 &v)
{
    return u"(%1, %2, %3)"_s.arg(v.x, 0, 'f', 3).arg(v.y, 0, 'f', 3).arg(v.z, 0, 'f', 3);
}

// Frames per tick in the tests of a moving hand (about 320 frames per second), and the most the
// hand may move between two of them: its way of half a metre over aimTicks, eased.
constexpr int stepsPerTick = 16;
constexpr double largestStep = 0.025;

// An animation whose hand has fully come to a launcher drawn with the power `draw`.
QcLocalAnimation drawnTo(double draw)
{
    QcLocalAnimation animation;
    for (int tick = 0; tick < 2 * QcHandTimers::aimTicks; ++tick) {
        animation.tick(qcNoItem, false, {}, draw);
    }
    return animation;
}

// Where the hand is `alpha` into the tick while the launcher reports `drawProgress`.
QcVec3 handOffset(const QcLocalAnimation &animation, double drawProgress, double alpha)
{
    QcEntityPose pose;
    pose.drawProgress = drawProgress;
    return QcAnimator::animateHand(pose, 0.0, animation.handTimers(alpha)).offset;
}

} // namespace

class TestBowPose : public QObject
{
    Q_OBJECT

    QcContentRegistry m_content;

    QcEntityMesher::ItemLook bow() const { return QcFigureLook::item(m_content, m_content.itemId(u"qtcraft:bow"_s).value_or(qcNoItem)); }

private slots:
    void initTestCase()
    {
        QVERIFY(QcBaseContent::loadInto(m_content));
        QVERIFY(m_content.finish().isEmpty());
    }

    // Both arms point along the view; the left one reaches across the chest to the string, so the
    // fists meet in front of the body instead of staying a shoulder apart.
    void archerArmsMeet()
    {
        QcEntityPose pose;
        pose.drawProgress = 1.0;
        const QcAnimator::BodyPose body = QcAnimator::animateBody(pose, 0.0);
        const QcAnimator::BoneRotation right = body.bone(QcBoneRole::RightArm);
        const QcAnimator::BoneRotation left = body.bone(QcBoneRole::LeftArm);
        QCOMPARE(right.pitch, 90.0);
        QCOMPARE(left.pitch, 90.0);
        QCOMPARE(right.roll, 0.0);
        QVERIFY(left.yaw > right.yaw + 20.0);

        const QcVec3 rightFist = fist(right, QcAnimator::driverShoulder);
        const QcVec3 leftFist = fist(left, {-QcAnimator::driverShoulder.x, QcAnimator::driverShoulder.y, 0.0});
        QVERIFY2(std::abs(rightFist.x - leftFist.x) < 0.3, qPrintable(text(rightFist) + text(leftFist)));
        QVERIFY2(rightFist.z < -0.5 && leftFist.z < -0.4, qPrintable(text(rightFist) + text(leftFist)));

        // The arms turn with the head.
        pose.headYaw = 30.0f;
        const QcAnimator::BodyPose turned = QcAnimator::animateBody(pose, 0.0);
        QCOMPARE(turned.bone(QcBoneRole::RightArm).yaw, right.yaw + 30.0);
        QCOMPARE(turned.bone(QcBoneRole::LeftArm).yaw, left.yaw + 30.0);
    }

    // Seen from outside, an archer's bow stands upright in the fist with its wood towards the
    // target (the figure faces -Z) and its string towards the face.
    void figuresAimTheBowUpright()
    {
        const QcEntityMesher::HeldPlacement placement = bow().thirdPersonDrawn;
        QcEntityPose pose;
        pose.drawProgress = 1.0;
        const QcAnimator::BoneRotation aiming = QcAnimator::animateBody(pose, 0.0).bone(QcBoneRole::RightArm);
        const QcVec3 axis = turnedBy(aiming, placed(placement, bowAxis));
        const QcVec3 belly = turnedBy(aiming, placed(placement, bowBelly));
        QVERIFY2(std::abs(axis.y) > 0.95, qPrintable(text(axis)));
        QVERIFY2(belly.z < -0.95, qPrintable(text(belly)));
    }

    // Merely held, the bow neither lies along the ground nor hangs straight down: it slants at the
    // side of the figure, the upper end forward, the wood forward and down.
    void figuresHoldTheBowSlanted()
    {
        const QcEntityMesher::HeldPlacement placement = bow().thirdPerson;
        const QcAnimator::BoneRotation hanging = QcAnimator::animateBody({}, 0.0).bone(QcBoneRole::RightArm);
        const QcVec3 axis = turnedBy(hanging, placed(placement, bowAxis));
        const QcVec3 belly = turnedBy(hanging, placed(placement, bowBelly));
        QVERIFY2(axis.y > 0.4 && axis.y < 0.85 && axis.z < -0.5, qPrintable(text(axis)));
        QVERIFY2(std::abs(axis.x) < 0.2, qPrintable(text(axis)));
        QVERIFY2(belly.y < 0.0 && belly.z < 0.0, qPrintable(text(belly)));
    }

    // In the first-person view the bow aims straight ahead (the camera looks along -Z), at rest and
    // drawn: the wood lies beyond the string and points neither left nor right. Drawn, the bow
    // stands nearly upright, turned just enough that its curve shows.
    void firstPersonStringFacesTheEye()
    {
        constexpr double mostSideways = 0.3;
        const QcEntityMesher::ItemLook look = bow();
        const QcVec3 rest = placed(look.firstPerson, bowBelly);
        QVERIFY2(rest.z < -0.9, qPrintable(text(rest)));
        QVERIFY2(std::abs(rest.x) < mostSideways, qPrintable(text(rest)));

        QcEntityPose pose;
        pose.drawProgress = 1.0;
        const QcAnimator::HandPose hand = QcAnimator::animateHand(pose, 0.0, {.aim = 1.0});
        QCOMPARE(hand.aim, 1.0);
        const auto camera = [&](const QcVec3 &direction) { return Turn(-hand.yaw)(Rotate(hand.pitch, 0.0)(placed(look.drawn, direction))); };
        QVERIFY2(camera(bowAxis).y > 0.9, qPrintable(text(camera(bowAxis))));
        QVERIFY2(camera(bowBelly).z < -0.9, qPrintable(text(camera(bowBelly))));
        QVERIFY2(std::abs(camera(bowBelly).x) < mostSideways, qPrintable(text(camera(bowBelly))));
        QVERIFY2(std::abs(camera(bowBelly).x) > 0.1, qPrintable(text(camera(bowBelly))));
    }

    // The hand comes to the drawn bow within aimTicks of its draw, never backwards on its way.
    void handBlendsIntoTheDraw()
    {
        QcLocalAnimation animation;
        double lastAim = 0.0;
        double draw = 0.0;
        for (int tick = 0; tick < 2 * QcHandTimers::aimTicks; ++tick) {
            animation.tick(qcNoItem, false, {}, draw);
            for (const double alpha : {0.0, 0.25, 0.5, 0.75}) {
                const double aim = animation.handTimers(alpha).aim;
                QVERIFY(aim >= lastAim);
                lastAim = aim;
            }
            draw = std::min(1.0, draw + 0.25);
        }
        QCOMPARE(animation.handTimers(0.0).aim, 1.0);
        QCOMPARE(animation.handTimers(0.0).lastDraw, 1.0);
    }

    // The shot resets the launcher at once, but the animation hears of it only with the next tick:
    // the hand stays where it was in every frame in between, and then goes back without a jump.
    void handStaysThroughTheShot()
    {
        for (const double draw : {1.0, 0.5}) {
            QcLocalAnimation animation = drawnTo(draw);
            const QcVec3 before = handOffset(animation, draw, 0.4);
            for (const double alpha : {0.4, 0.6, 0.9}) {
                const QcVec3 between = handOffset(animation, -1.0, alpha);
                QVERIFY2((between - before).length() < 0.001, qPrintable(text(before) + text(between)));
            }

            animation.tick(qcNoItem, false, {}, -1.0);
            const QcVec3 after = handOffset(animation, -1.0, 0.0);
            QVERIFY2((after - before).length() < 0.001, qPrintable(text(before) + text(after)));

            double lastAim = 1.0;
            QcVec3 last = after;
            for (int tick = 0; tick < QcHandTimers::aimTicks; ++tick) {
                for (int step = 0; step < stepsPerTick; ++step) {
                    const double alpha = static_cast<double>(step) / stepsPerTick;
                    QVERIFY(animation.handTimers(alpha).aim <= lastAim);
                    lastAim = animation.handTimers(alpha).aim;
                    const QcVec3 offset = handOffset(animation, -1.0, alpha);
                    QVERIFY2((offset - last).length() < largestStep, qPrintable(text(last) + text(offset)));
                    last = offset;
                }
                animation.tick(qcNoItem, false, {}, -1.0);
            }
            QCOMPARE(animation.handTimers(0.0).aim, 0.0);
            QCOMPARE(animation.handTimers(0.0).lastDraw, 0.0);
        }
    }

    // Drawing again while the hand still goes back: the pull and the dip of the last draw fade
    // into the new one instead of jumping to its weak start.
    void drawingAgainWhileTheAimFalls()
    {
        QcLocalAnimation animation = drawnTo(1.0);
        animation.tick(qcNoItem, false, {}, -1.0);
        animation.tick(qcNoItem, false, {}, -1.0);
        const double start = 0.02;
        QVERIFY((handOffset(animation, start, 0.5) - handOffset(animation, -1.0, 0.5)).length() < 0.002);

        QcVec3 last = handOffset(animation, start, 0.5);
        for (int tick = 0; tick < 2 * QcHandTimers::aimTicks; ++tick) {
            for (int step = tick == 0 ? stepsPerTick / 2 : 0; step < stepsPerTick; ++step) {
                const QcVec3 offset = handOffset(animation, start, static_cast<double>(step) / stepsPerTick);
                QVERIFY2((offset - last).length() < largestStep, qPrintable(text(last) + text(offset)));
                last = offset;
            }
            animation.tick(qcNoItem, false, {}, start);
        }
        QCOMPARE(animation.handTimers(0.0).lastDraw, start);
    }

    // A fresh draw long after the last one starts from nothing.
    void aFreshDrawForgetsTheLastOne()
    {
        QcLocalAnimation animation = drawnTo(1.0);
        for (int tick = 0; tick <= QcHandTimers::aimTicks + 1; ++tick) {
            animation.tick(qcNoItem, false, {}, -1.0);
        }
        animation.tick(qcNoItem, false, {}, 0.05);
        QCOMPARE(animation.handTimers(0.0).lastDraw, 0.05);
    }

    // Another item in the hand (switched, dropped or a broken bow) ends the pose of the launcher at
    // once: it does not run on over the new item.
    void anotherItemEndsTheDraw()
    {
        constexpr QcItemId bowItem = 3;
        constexpr QcItemId sword = 7;
        for (const QcItemId next : {sword, qcNoItem}) {
            QcLocalAnimation drawing;
            QcLocalAnimation resting;
            for (int tick = 0; tick < 2 * QcHandTimers::aimTicks; ++tick) {
                drawing.tick(bowItem, false, {}, 1.0);
                resting.tick(bowItem, false, {}, -1.0);
            }
            QCOMPARE(drawing.handTimers(0.0).aim, 1.0);

            // The launcher ends right after the tick that sees the new item.
            drawing.tick(next, false, {}, 1.0);
            resting.tick(next, false, {}, -1.0);
            for (int tick = 0; tick < QcHandTimers::aimTicks; ++tick) {
                for (const double alpha : {0.0, 0.5, 0.9}) {
                    QCOMPARE(drawing.handTimers(alpha).aim, 0.0);
                    const QcAnimator::HandPose hand = QcAnimator::animateHand({}, 0.0, drawing.handTimers(alpha));
                    const QcAnimator::HandPose rest = QcAnimator::animateHand({}, 0.0, resting.handTimers(alpha));
                    QVERIFY2((hand.offset - rest.offset).length() < 1e-9, qPrintable(text(hand.offset) + text(rest.offset)));
                    QCOMPARE(hand.yaw, rest.yaw);
                }
                drawing.tick(next, false, {}, -1.0);
                resting.tick(next, false, {}, -1.0);
            }
        }
    }

    // The tremble starts once the draw passes a tenth, grows with it and fades with the hand's way
    // to the drawn placement.
    void trembleGrowsWithTheDraw()
    {
        const auto shake = [](double draw, double progress) {
            double lowest = 1.0;
            double highest = -1.0;
            for (int step = 0; step < 100; ++step) {
                QcAnimator::HandPose hand;
                hand.holdDrawn(draw, progress, 0.0, step * 0.01);
                lowest = std::min(lowest, hand.offset.y);
                highest = std::max(highest, hand.offset.y);
            }
            return highest - lowest;
        };
        QCOMPARE(shake(0.0, 1.0), 0.0);
        QCOMPARE(shake(0.1, 1.0), 0.0);
        QVERIFY(shake(0.5, 1.0) > 0.001);
        QVERIFY(shake(1.0, 1.0) > shake(0.5, 1.0) + 0.001);
        QVERIFY(shake(1.0, 0.5) < 0.75 * shake(1.0, 1.0));
        QCOMPARE(shake(1.0, 0.0), 0.0);
    }

    // Drawing narrows the view by up to 15 % with the square of the draw time, scaled by the
    // setting for field-of-view effects; the share is the inverse of the draw's power curve.
    void drawingNarrowsTheView()
    {
        const QcLauncherDefinition launcher;
        for (const double share : {0.0, 0.25, 0.5, 1.0}) {
            QVERIFY(std::abs(QcLauncherDefinition::shareOf(launcher.powerAfter(share * launcher.drawSeconds)) - share) < 1e-9);
        }

        // From the pose: nothing drawn is no narrowing, a full draw all of it.
        QCOMPARE(QcFovEffect::drawShare(-1.0), 0.0);
        QCOMPARE(QcFovEffect::drawShare(0.0), 0.0);
        QCOMPARE(QcFovEffect::drawShare(1.0), 1.0);
        QVERIFY(std::abs(QcFovEffect::drawShare(launcher.powerAfter(0.5 * launcher.drawSeconds)) - 0.5) < 1e-9);

        QcFovEffect effect;
        QCOMPARE(effect.target({.draw = QcFovEffect::drawShare(-1.0)}), 1.0);
        QCOMPARE(effect.target({.draw = 0.0}), 1.0);
        QVERIFY(std::abs(effect.target({.draw = 1.0}) - (1.0 - QcFovEffect::drawNarrowing)) < 1e-9);
        QVERIFY(std::abs(effect.target({.draw = 0.5}) - (1.0 - QcFovEffect::drawNarrowing * 0.25)) < 1e-9);
        double last = 1.0;
        for (int tenth = 0; tenth <= 10; ++tenth) {
            const double narrowed = effect.target({.draw = tenth / 10.0});
            QVERIFY(narrowed <= last);
            last = narrowed;
        }
        effect.setShare(0.5);
        QVERIFY(std::abs(effect.target({.draw = 1.0}) - (1.0 - QcFovEffect::drawNarrowing * 0.5)) < 1e-9);
        effect.setShare(0.0);
        QCOMPARE(effect.target({.draw = 1.0}), 1.0);
    }
};

QTEST_GUILESS_MAIN(TestBowPose)
#include "test_bow_pose.moc"

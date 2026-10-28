#include "clientcore/meshing/QcAnimator.h"

#include "clientcore/meshing/QcBoatModel.h"
#include "clientcore/meshing/QcHandSwing.h"

#include "common/physics/QcPhysics.h"
#include "common/world/QcBoatBody.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace QcAnimator {

namespace {

// Strides per metre walked and the swing amplitude at walking and sprinting speed.
constexpr double strideFrequency = 1.6;
constexpr double walkAmplitude = 38.0;
constexpr double sprintAmplitude = 55.0;
// Pivot of the whole body for leaning: the hips of a 1.8 m humanoid.
constexpr double hipHeight = 0.7;
// How far the head turns aside against the body.
constexpr double maxHeadYaw = 80.0;
// Lying on the back turns the body around this height above the feet: the back rests just above
// the place the entity stands on (the top of its bed); the server's box of a lying body follows it.
constexpr double sleepPivot = QcPhysics::lyingPivot;
// How far the empty arm punches forward in the middle of a swing, in metres.
constexpr double emptyPunch = 0.14;

double smooth(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

} // namespace

double swingLift(double progress)
{
    const double p = std::clamp(progress, 0.0, 1.0);
    if (p < swingTop) {
        return smooth(p / swingTop);
    }
    if (p < swingBottom) {
        // Accelerates into the blow like a real strike.
        const double t = (p - swingTop) / (swingBottom - swingTop);
        return 1.0 - 2.0 * t * t;
    }
    return -1.0 + smooth((p - swingBottom) / (1.0 - swingBottom));
}

namespace {

// The limbs of a body that swims, jumps or falls, or stands or walks on the ground (`stride` and
// `amplitude` of the walk, `idle` the sway at rest), the body pivot at the hips.
BodyPose movingLimbs(const QcEntityPose &pose, double time, double stride, double amplitude, double idle)
{
    BodyPose result;
    result.bodyPivot = {0.0, hipHeight, 0.0};
    if (pose.inWater && !pose.onGround) {
        // Swimming: the body lies forward, arms sweep in wide strokes, legs flutter.
        result.bodyPitch = 70.0 * smooth(std::max(0.3, pose.speed / QcPhysics::swimSpeed));
        const double stroke = std::sin(time * 4.0);
        result.bone(QcBoneRole::RightArm) = {150.0 + stroke * 30.0, 10.0};
        result.bone(QcBoneRole::LeftArm) = {150.0 - stroke * 30.0, -10.0};
        result.bone(QcBoneRole::RightLeg).pitch = std::sin(time * 8.0) * 15.0;
        result.bone(QcBoneRole::LeftLeg).pitch = -std::sin(time * 8.0) * 15.0;
    } else if (!pose.onGround) {
        // Jumping or falling: arms out to the sides, one leg forward.
        result.bone(QcBoneRole::RightArm) = {-10.0, 18.0};
        result.bone(QcBoneRole::LeftArm) = {-10.0, -18.0};
        result.bone(QcBoneRole::RightLeg).pitch = 25.0;
        result.bone(QcBoneRole::LeftLeg).pitch = -10.0;
    } else {
        // Standing or walking: legs and arms swing against each other; idle sway at rest.
        result.bone(QcBoneRole::RightLeg).pitch = stride * amplitude;
        result.bone(QcBoneRole::LeftLeg).pitch = -stride * amplitude;
        result.bone(QcBoneRole::RightArm) = {-stride * amplitude * 0.8, 3.0 + idle};
        result.bone(QcBoneRole::LeftArm) = {stride * amplitude * 0.8, -3.0 - idle};
    }
    return result;
}

// The right arm during a swing: raised high over the head, struck down in front of the body, settled
// back to its `rest` pitch.
BoneRotation swingingArm(double swingProgress, double rest)
{
    constexpr double raised = 160.0;
    constexpr double struck = 30.0;
    const double progress = std::clamp(swingProgress, 0.0, 1.0);
    const double lift = swingLift(progress);
    double pitch = struck + (raised - struck) * (lift + 1.0) / 2.0;
    if (progress < swingTop) {
        pitch = rest + (raised - rest) * lift;
    } else if (progress > swingBottom) {
        pitch = rest + (struck - rest) * -lift;
    }
    return {pitch, -10.0 * std::abs(lift)};
}

} // namespace

double legSwing(double walked, double speed, double legLength)
{
    constexpr double legLengthsPerStride = 1.8;
    if (legLength <= 0.0) {
        return 0.0;
    }
    const double stride = std::sin(walked / (legLengthsPerStride * legLength) * 2.0 * M_PI);
    return stride * maxLegSwing * std::clamp(speed / fullSwingSpeed, 0.0, 1.0);
}

BodyPose animateBody(const QcEntityPose &pose, double time)
{
    const double stride = std::sin(pose.walkDistance * strideFrequency * M_PI);
    const double amplitude = (pose.sprinting ? sprintAmplitude : walkAmplitude)
                             * std::clamp(pose.speed / QcPhysics::walkSpeed, 0.0, 1.0);
    const double idle = std::sin(time * 1.7) * 3.0;
    BodyPose result = movingLimbs(pose, time, stride, amplitude, idle);

    if (pose.inBoat) {
        result.rowBoat(pose.rowPhase);
    } else if (pose.riding || pose.body == QcPose::Sitting) {
        // Sitting: the legs forward and a little apart, the arms resting.
        result.bone(QcBoneRole::RightLeg) = {80.0, 8.0};
        result.bone(QcBoneRole::LeftLeg) = {80.0, -8.0};
        result.bone(QcBoneRole::RightArm) = {20.0, 5.0 + idle};
        result.bone(QcBoneRole::LeftArm) = {20.0, -5.0 - idle};
    }
    if (pose.armsForward) {
        result.bone(QcBoneRole::RightArm) = {90.0 + stride * amplitude * 0.15, 0.0};
        result.bone(QcBoneRole::LeftArm) = {90.0 - stride * amplitude * 0.15, 0.0};
    }
    if (pose.drawProgress >= 0.0) {
        result.aimBow(pose);
    }
    if (pose.sneaking && pose.onGround && !pose.inWater) {
        result.bodyPitch = 12.0;
        result.bodyLift = -0.12;
    }
    if (pose.swingProgress >= 0.0) {
        result.bone(QcBoneRole::RightArm) = swingingArm(pose.swingProgress, result.bone(QcBoneRole::RightArm).pitch);
    }
    if (pose.hurtProgress >= 0.0) {
        result.bodyPitch -= 12.0 * std::sin(std::clamp(pose.hurtProgress, 0.0, 1.0) * M_PI);
    }

    // The head follows the view pitch: looking up tilts its top backwards, looking down forwards.
    result.bone(QcBoneRole::Head).pitch = std::clamp(static_cast<double>(pose.headPitch), -80.0, 80.0);
    result.bone(QcBoneRole::Head).yaw = std::clamp(static_cast<double>(pose.headYaw), -maxHeadYaw, maxHeadYaw);
    if (pose.body == QcPose::Lying) {
        result.lieOnBack(idle);
    }
    if (pose.deathProgress >= 0.0) {
        // Topple sideways around the feet.
        result.bodyPivot = {};
        result.bodyPitch = 0.0;
        result.bodyRoll = 90.0 * smooth(pose.deathProgress);
    }
    return result;
}

HandPose animateHand(const QcEntityPose &pose, double time, const QcHandTimers &timers)
{
    HandPose hand;
    // Walking bob: the hand traces a small arc with each stride; breathing at rest.
    const double moving = std::clamp(pose.speed / QcPhysics::walkSpeed, 0.0, 1.0) * (pose.onGround ? 1.0 : 0.0);
    const double phase = pose.walkDistance * strideFrequency * M_PI;
    hand.offset.x = std::sin(phase) * 0.03 * moving;
    hand.offset.y = -std::abs(std::cos(phase)) * 0.025 * moving + std::sin(time * 1.7) * 0.004;

    if (!pose.onGround && !pose.inWater) {
        // In the air the arm lags behind and rises.
        hand.offset.y += 0.03;
        hand.pitch += 6.0;
    }
    if (timers.landing >= 0.0) {
        hand.offset.y -= 0.05 * std::sin(std::clamp(timers.landing, 0.0, 1.0) * M_PI);
    }
    if (timers.switchProgress >= 0.0) {
        // Down and up again while the new item is drawn.
        hand.offset.y -= 0.35 * std::sin(std::clamp(timers.switchProgress, 0.0, 1.0) * M_PI);
    }

    // Arm and item turn as one about the shoulder (decision E-881): mining loops the full swing, a
    // click (attacking, placing, using) makes the short one. A swing placement of the content, if
    // any, is reached in the middle of every swing; the empty arm punches forward meanwhile.
    double swingProgress = -1.0;
    QcHandSwing::Style style = QcHandSwing::Style::Click;
    if (pose.swingProgress >= 0.0) {
        swingProgress = pose.swingProgress;
        style = timers.mining ? QcHandSwing::Style::Mining : QcHandSwing::Style::Click;
    } else if (timers.use >= 0.0) {
        swingProgress = timers.use;
    }
    if (swingProgress >= 0.0) {
        const QcHandSwing::Angles swing = QcHandSwing::at(swingProgress, style);
        const double middle = std::sin(std::clamp(swingProgress, 0.0, 1.0) * M_PI);
        hand.pitch += swing.pitch;
        hand.yaw += swing.yaw;
        hand.roll += swing.roll;
        hand.reach = swing.reach;
        hand.swing = middle;
        hand.punch = emptyPunch * middle;
    }
    if (pose.drawProgress >= 0.0) {
        // Drawing a bow: the hand comes to the middle and pulls back, trembling at a full draw.
        const double draw = std::clamp(pose.drawProgress, 0.0, 1.0);
        const double tremble = draw >= 1.0 ? std::sin(time * 40.0) * 0.004 : 0.0;
        hand.offset.x -= 0.18;
        hand.offset.y += 0.06 + tremble;
        hand.offset.z += 0.12 * draw;
        hand.yaw += 20.0;
        hand.pitch += 10.0;
    }
    return hand;
}

void BodyPose::rowBoat(double rowPhase)
{
    // The thighs level and forward, a little apart (QcBoatBody: the hips just above the floor).
    bone(QcBoneRole::RightLeg) = {90.0, 4.0};
    bone(QcBoneRole::LeftLeg) = {90.0, -4.0};

    // Each arm points from its shoulder to the grip of its paddle, in the boat's frame (the figure
    // faces the boat's way, its feet on the driver's seat).
    const QcBoatModel::Paddle paddle = QcBoatModel::paddleAt(rowPhase);
    const QcVec3 figure{0.0, QcBoatBody::floor + QcBoatBody::seatHeight - QcBoatBody::hipHeight, -QcBoatBody::driverAlong};
    for (const int side : {1, -1}) {
        const QcVec3 shoulder = figure + QcVec3{side * driverShoulder.x, driverShoulder.y, 0.0};
        const QcVec3 reach = QcBoatModel::gripOf(side, paddle) - shoulder;
        const double pitch = qRadiansToDegrees(std::atan2(-reach.z, -reach.y));
        const double roll = qRadiansToDegrees(std::atan2(reach.x, std::hypot(reach.y, reach.z)));
        bone(side > 0 ? QcBoneRole::RightArm : QcBoneRole::LeftArm) = {pitch, roll};
    }
}

void BodyPose::lieOnBack(double idle)
{
    bones = {};
    bone(QcBoneRole::RightArm).roll = 4.0 + idle * 0.3;
    bone(QcBoneRole::LeftArm).roll = -4.0 - idle * 0.3;
    bodyPivot = {0.0, sleepPivot, 0.0};
    bodyPitch = -90.0;
    bodyLift = 0.0;
}

void BodyPose::aimBow(const QcEntityPose &pose)
{
    const double aim = 90.0 + pose.headPitch;
    bone(QcBoneRole::LeftArm) = {aim, 12.0};
    bone(QcBoneRole::RightArm) = {aim - 8.0 * pose.drawProgress, -25.0 - 15.0 * pose.drawProgress};
}

} // namespace QcAnimator

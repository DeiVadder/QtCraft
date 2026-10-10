#include "QcAnimator.h"

#include "QcPhysics.h"

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

double smooth(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

BoneRotation &bone(BodyPose &pose, QcBoneRole role)
{
    return pose.bones[std::size_t(role)];
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

BodyPose animateBody(const QcEntityPose &pose, double time)
{
    BodyPose result;
    result.bodyPivot = {0.0, hipHeight, 0.0};
    const double stride = std::sin(pose.walkDistance * strideFrequency * M_PI);
    const double amplitude = (pose.sprinting ? sprintAmplitude : walkAmplitude)
                             * std::clamp(pose.speed / QcPhysics::walkSpeed, 0.0, 1.0);
    const double idle = std::sin(time * 1.7) * 3.0;

    if (pose.inWater && !pose.onGround) {
        // Swimming: the body lies forward, arms sweep in wide strokes, legs flutter.
        result.bodyPitch = 70.0 * smooth(std::max(0.3, pose.speed / QcPhysics::swimSpeed));
        const double stroke = std::sin(time * 4.0);
        bone(result, QcBoneRole::RightArm) = {150.0 + stroke * 30.0, 10.0};
        bone(result, QcBoneRole::LeftArm) = {150.0 - stroke * 30.0, -10.0};
        bone(result, QcBoneRole::RightLeg).pitch = std::sin(time * 8.0) * 15.0;
        bone(result, QcBoneRole::LeftLeg).pitch = -std::sin(time * 8.0) * 15.0;
    } else if (!pose.onGround) {
        // Jumping or falling: arms out to the sides, one leg forward.
        bone(result, QcBoneRole::RightArm) = {-10.0, 18.0};
        bone(result, QcBoneRole::LeftArm) = {-10.0, -18.0};
        bone(result, QcBoneRole::RightLeg).pitch = 25.0;
        bone(result, QcBoneRole::LeftLeg).pitch = -10.0;
    } else {
        // Standing or walking: legs and arms swing against each other; idle sway at rest.
        bone(result, QcBoneRole::RightLeg).pitch = stride * amplitude;
        bone(result, QcBoneRole::LeftLeg).pitch = -stride * amplitude;
        bone(result, QcBoneRole::RightArm) = {-stride * amplitude * 0.8, 3.0 + idle};
        bone(result, QcBoneRole::LeftArm) = {stride * amplitude * 0.8, -3.0 - idle};
    }

    if (pose.armsForward) {
        bone(result, QcBoneRole::RightArm) = {90.0 + stride * amplitude * 0.15, 0.0};
        bone(result, QcBoneRole::LeftArm) = {90.0 - stride * amplitude * 0.15, 0.0};
    }
    if (pose.sneaking && pose.onGround && !pose.inWater) {
        result.bodyPitch = 12.0;
        result.bodyLift = -0.12;
    }
    if (pose.swingProgress >= 0.0) {
        // Raise the right arm high over the head, strike down in front of the body, settle back.
        constexpr double raised = 160.0;
        constexpr double struck = 30.0;
        const double rest = bone(result, QcBoneRole::RightArm).pitch;
        const double progress = std::clamp(pose.swingProgress, 0.0, 1.0);
        const double lift = swingLift(progress);
        double pitch = struck + (raised - struck) * (lift + 1.0) / 2.0;
        if (progress < swingTop) {
            pitch = rest + (raised - rest) * lift;
        } else if (progress > swingBottom) {
            pitch = rest + (struck - rest) * -lift;
        }
        bone(result, QcBoneRole::RightArm) = {pitch, -10.0 * std::abs(lift)};
    }
    if (pose.hurtProgress >= 0.0) {
        result.bodyPitch -= 12.0 * std::sin(std::clamp(pose.hurtProgress, 0.0, 1.0) * M_PI);
    }

    // The head follows the view pitch: looking up tilts its top backwards, looking down forwards.
    bone(result, QcBoneRole::Head).pitch = std::clamp(double(pose.headPitch), -80.0, 80.0);
    if (pose.deathProgress >= 0.0) {
        // Topple sideways around the feet.
        result.bodyPivot = {};
        result.bodyPitch = 0.0;
        result.bodyRoll = 90.0 * smooth(pose.deathProgress);
    }
    return result;
}

HandPose animateHand(const QcEntityPose &pose, double time, double switchProgress, double landing, double useProgress)
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
    if (landing >= 0.0) {
        hand.offset.y -= 0.05 * std::sin(std::clamp(landing, 0.0, 1.0) * M_PI);
    }
    if (switchProgress >= 0.0) {
        // Down and up again while the new item is drawn.
        hand.offset.y -= 0.35 * std::sin(std::clamp(switchProgress, 0.0, 1.0) * M_PI);
    }

    if (pose.swingProgress >= 0.0) {
        // Up first, then down and inwards across the crosshair, then back to rest; small angles
        // keep the arm and the held item on the screen.
        const double lift = swingLift(pose.swingProgress);
        const double down = std::max(0.0, -lift);
        const double thrust = std::sin(std::clamp(pose.swingProgress, 0.0, 1.0) * M_PI);
        hand.offset.x -= 0.08 * down;
        hand.offset.y += 0.04 * lift;
        hand.offset.z -= 0.10 * thrust;
        hand.pitch += lift >= 0.0 ? 22.0 * lift : 12.0 * lift;
        hand.yaw += 18.0 * down;
    }
    if (useProgress >= 0.0) {
        const double push = std::sin(std::clamp(useProgress, 0.0, 1.0) * M_PI);
        hand.offset.z -= 0.12 * push;
        hand.pitch += 15.0 * push;
    }
    return hand;
}

} // namespace QcAnimator

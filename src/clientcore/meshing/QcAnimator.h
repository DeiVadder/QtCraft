#pragma once

#include "QcEntityPose.h"
#include "QcModelDefinition.h"
#include "QcVec3.h"

#include <array>

// Turns a pose into bone rotations for skeleton models and into the first-person hand motion.
// Pure functions of their inputs, so animations are deterministic and testable.
namespace QcAnimator {

// Rotation of a bone around its pivot in degrees. Positive pitch swings a hanging limb forward
// (towards the facing direction, -Z); positive roll lifts it outwards to the entity's right (+X).
struct BoneRotation
{
    double pitch = 0.0;
    double roll = 0.0;
};

struct BodyPose
{
    // Indexed by QcBoneRole.
    std::array<BoneRotation, 7> bones;
    // Whole-body rotation around `bodyPivot` (leaning when swimming or sneaking, falling over when
    // dying) and a vertical offset (crouching).
    double bodyPitch = 0.0;
    double bodyRoll = 0.0;
    QcVec3 bodyPivot;
    double bodyLift = 0.0;

    const BoneRotation &bone(QcBoneRole role) const { return bones[std::size_t(role)]; }
};

// Where the first-person arm and held item sit relative to their rest position, in the camera's
// frame (x right, y up, z backwards) in metres, and how far the arm is turned (degrees).
struct HandPose
{
    QcVec3 offset;
    double pitch = 0.0;
    double yaw = 0.0;
};

// `time` is a free-running clock in seconds for idle motion.
BodyPose animateBody(const QcEntityPose &pose, double time);

// `switchProgress` runs from 0 to 1 after the held item changed (negative when idle); `landing`
// from 0 to 1 after touching the ground (negative otherwise); `useProgress` like swingProgress
// for placing or eating.
HandPose animateHand(const QcEntityPose &pose, double time, double switchProgress, double landing, double useProgress);

} // namespace QcAnimator

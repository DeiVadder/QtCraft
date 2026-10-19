#pragma once

#include "clientcore/meshing/QcEntityPose.h"
#include "common/content/definitions/QcModelDefinition.h"
#include "common/core/QcVec3.h"

#include <array>

// Turns a pose into bone rotations for skeleton models and into the first-person hand motion.
// Pure functions of their inputs, so animations are deterministic and testable.
namespace QcAnimator {

// Rotation of a bone around its pivot in degrees. Positive pitch swings a hanging limb forward
// (towards the facing direction, -Z); positive roll lifts it outwards to the entity's right (+X);
// positive yaw turns it around the vertical towards the entity's right (the head looking aside).
// Applied in this order: pitch, roll, yaw.
struct BoneRotation
{
    double pitch = 0.0;
    double roll = 0.0;
    double yaw = 0.0;
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
    // Cape (decision E-240) against the back in degrees: its free end lifted backwards (0 hangs
    // along the back, 90 points straight back) and swung to the entity's left. animateBody leaves
    // them 0; QcCapeSwing gives them from the movement.
    double capeLift = 0.0;
    double capeSway = 0.0;

    const BoneRotation &bone(QcBoneRole role) const { return bones[std::size_t(role)]; }
    BoneRotation &bone(QcBoneRole role) { return bones[std::size_t(role)]; }

    // On the back around a point just above the feet, the limbs straight, the head on the pillow;
    // only the breath (`idle`) moves the arms.
    void lieOnBack(double idle);
    // Aiming a bow (decision E-425): the left arm holds it out along the view, the right one pulls
    // the string back towards the face.
    void aimBow(const QcEntityPose &pose);
};

// Where the first-person arm and held item sit relative to their rest position, in the camera's
// frame (x right, y up, z backwards) in metres, and how far the arm is turned (degrees).
struct HandPose
{
    QcVec3 offset;
    double pitch = 0.0;
    double yaw = 0.0;
};

// Share of a mining or attack swing at which the arm is highest and lowest.
inline constexpr double swingTop = 0.2;
inline constexpr double swingBottom = 0.6;

// Height of the swinging arm over its progress (0..1): rises quickly to 1 at swingTop, strikes
// down to -1 at swingBottom, and settles back to 0 at the end. Shared by the first-person hand and
// the body, so both strike from top to bottom.
double swingLift(double progress);

// `time` is a free-running clock in seconds for idle motion.
BodyPose animateBody(const QcEntityPose &pose, double time);

// `switchProgress` runs from 0 to 1 after the held item changed (negative when idle); `landing`
// from 0 to 1 after touching the ground (negative otherwise); `useProgress` like swingProgress
// for placing or eating.
HandPose animateHand(const QcEntityPose &pose, double time, double switchProgress, double landing, double useProgress);

} // namespace QcAnimator

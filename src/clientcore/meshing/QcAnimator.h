#pragma once

#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/player/QcHandTimers.h"
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

    const BoneRotation &bone(QcBoneRole role) const { return bones[static_cast<std::size_t>(role)]; }
    BoneRotation &bone(QcBoneRole role) { return bones[static_cast<std::size_t>(role)]; }

    // On the back around a point just above the feet, the limbs straight, the head on the pillow;
    // only the breath (`idle`) moves the arms.
    void lieOnBack(double idle);
    // Aiming a bow (decision E-425): the left arm holds it out along the view, the right one pulls
    // the string back towards the face.
    void aimBow(const QcEntityPose &pose);
};

// Where the first-person arm and held item sit relative to their rest position, in the camera's
// frame (x right, y up, z backwards) in metres, and how far arm and item are turned together about
// the shoulder (degrees; QcHandSwing::Angles). `swing` (0..1) is how far the held item has turned
// from its rest placement to its swing placement (ItemLook::swing, decision E-831); the base
// content gives none, so arm and item swing as one rigid body (decision E-881). `roll` turns an
// arm holding an item about the fist and `reach` (0..1, how far into the strike) moves it as a
// whole towards the middle of the screen (QcEntityMesher::heldStrikeShift); `punch` (metres) moves
// the empty arm, and only it, forward as a whole: a fist strikes ahead, not just down.
struct HandPose
{
    QcVec3 offset;
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
    double swing = 0.0;
    double reach = 0.0;
    double punch = 0.0;
};

// Share of a mining or attack swing at which the arm is highest and lowest.
inline constexpr double swingTop = 0.2;
inline constexpr double swingBottom = 0.6;

// Height of a figure's swinging arm over its progress (0..1): rises quickly to 1 at swingTop,
// strikes down to -1 at swingBottom, and settles back to 0 at the end. The first-person hand
// swings by QcHandSwing.
double swingLift(double progress);

// `time` is a free-running clock in seconds for idle motion.
BodyPose animateBody(const QcEntityPose &pose, double time);

// How a creature of static boxes walks (decision E-888): the legs' swing in degrees (positive moves
// the leg forward that QcBoxLegs gives phase 1), after `walked` metres at `speed` m/s on legs
// `legLength` metres long (as drawn). One stride cycle covers 1.8 leg lengths, so short legs step
// quickly; the swing grows with the speed up to `maxLegSwing` at `fullSwingSpeed` and more
// (QcBoxLegs::maxSwing narrows it where front and hind feet would meet).
inline constexpr double maxLegSwing = 32.0;
inline constexpr double fullSwingSpeed = 1.5;
double legSwing(double walked, double speed, double legLength);

// The hand of the own player: `pose.swingProgress` swings it (the mining loop or a click's short
// swing, by `timers.mining`), `timers` switch the item, land and use (placing or eating: a click's
// swing too); `time` drives the breathing.
HandPose animateHand(const QcEntityPose &pose, double time, const QcHandTimers &timers);

} // namespace QcAnimator

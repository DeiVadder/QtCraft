// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"

#include "clientcore/light/QcEntityLight.h"
#include "clientcore/meshing/QcAnimator.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcBoxLegs.h"
#include "clientcore/meshing/QcEntityMesh.h"
#include "clientcore/meshing/QcSpriteShape.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/content/definitions/QcAppearanceDefinition.h"
#include "common/content/definitions/QcHoldDisplay.h"
#include "common/content/definitions/QcMobDefinition.h"
#include "common/content/definitions/QcModelDefinition.h"

#include <QRectF>

#include <array>
#include <functional>
#include <memory>

// Builds the triangles of entities: static box models, animated skeleton models with outfits and
// hair, dropped items and the first-person hand. Rebuilt every frame; entities are few.
namespace QcEntityMesher {

using UvLookup = std::function<QRectF(const QString &texture)>;

// Where a held item sits around the grip (decision E-831), resolved from the content's hold
// transforms (QcHoldTransform): a point of the item (a sprite one unit across, a block one unit per
// edge, relative to the grip) is scaled by `scale`, turned by `rotate` (degrees: Turn by y, then
// Rotate by x and z) and moved by `translate`, in metres in the frame of the fist.
struct HeldPlacement
{
    QcVec3 translate;
    QcVec3 rotate;
    // A quarter of a block per unit when no pack sets a size.
    QcVec3 scale{0.25, 0.25, 0.25};

    // `weight` (0..1) of the way from this placement to `other`: translate and rotate blended per
    // axis as written, the scale kept, so the grip stays in the fist while the item turns.
    [[nodiscard]] HeldPlacement towards(const HeldPlacement &other, double weight) const
    {
        return {translate + (other.translate - translate) * weight, rotate + (other.rotate - rotate) * weight, scale};
    }
};

// Appearance of an item: a small cube with the block's face textures for placeable cube blocks,
// otherwise a sprite of the item texture.
struct ItemLook
{
    bool cube = false;
    // Indexed by QcBlockFace; used for cubes.
    std::array<QString, 6> faces;
    // Boxes of a block shape (0..1) drawn instead of the full cube (stairs, fences); empty for cubes.
    QList<QcAabb> boxes;
    QString sprite;
    // Light the item gives off itself (0..1, a torch): it is never drawn darker than that.
    float glow = 0.0f;
    // Where a hand takes a sprite; a block is taken by the middle of its boxes.
    QcHoldGrip grip = QcHoldGrip::Base;
    HeldPlacement firstPerson;
    HeldPlacement thirdPerson;
    // The first-person placement in the middle of a swing (QcHoldDisplay::swing); the hand blends
    // towards it by HandPose::swing.
    HeldPlacement swing;
    // The sprite's opaque texels; held sprites with a shape are drawn with thickness, without one
    // flat.
    std::shared_ptr<const QcSpriteShape> shape;
};

// What a skeleton model looks like: the model, an outfit, an optional hair style and cape, the
// colours that tinted layers use, and a uniform scale (models are built for 1.8 m).
struct ModelLook
{
    const QcModelDefinition *model = nullptr;
    const QcAppearanceDefinition *outfit = nullptr;
    const QcAppearanceDefinition *hair = nullptr;
    QcAppearance colors;
    double scale = 1.0;
    // Hangs from the top of the body bone's back, turned by BodyPose::capeLift/capeSway.
    const QcAppearanceDefinition *cape = nullptr;
    // Extra gap between the back and the cape in metres, on top of the cape's own offset: the
    // thickness of whatever the torso wears over the outfit (armour layers), so the cape stays
    // outside it.
    double capeClearance = 0.0;

    bool isValid() const { return model && outfit; }
};

// An extra textured box worn on a bone of a skeleton model, in model space like the bone's own
// boxes: it turns with the bone and takes the figure's light and hurt tint. Armour layers and
// parts are attachments (QcArmourLook, decision E-235); anything else worn on the body can use
// them too. `front`, when set, textures the face looking forward (-Z) instead of `texture`.
// Attachments on a bone the model lacks follow the whole body.
struct Attachment
{
    QcBoneRole bone = QcBoneRole::Body;
    QcAabb box;
    QString texture;
    QString front;
};

// Bit of a bone role in Figure::hiddenBones.
constexpr quint8 boneBit(QcBoneRole role)
{
    return static_cast<quint8>(1u << static_cast<unsigned>(role));
}

// A skeleton model as drawn: its look, the item in its right hand and what it wears (both optional,
// not owned). Bones whose role bit is set in `hiddenBones` are left out with their attachments (an
// armour stand without arms or base plate).
struct Figure
{
    ModelLook look;
    const ItemLook *held = nullptr;
    const QList<Attachment> *attachments = nullptr;
    quint8 hiddenBones = 0;
};

// Edge length of a dropped cube and of a sprite.
inline constexpr double itemCubeSize = 0.25;
inline constexpr double itemSpriteSize = 0.45;
// Where the empty first-person shoulder sits in the camera's frame (x right, y up, z backwards,
// metres) and how large the empty arm is drawn (decision E-722).
inline constexpr QcVec3 emptyHandShoulder{0.4, -0.4, -0.08};
inline constexpr double emptyHandScale = 0.8;
// Metres the empty arm rises per metre of its punch forward (HandPose::punch), so the fist strikes
// ahead over the swing's downward turn instead of dropping onto the hotbar.
inline constexpr double emptyPunchRise = 1.4;
// The first-person arm holding an item (decision E-831): the grip in the camera's frame, low right
// of the crosshair; the arm's pitch (degrees, 90 points it forward) and turn (negative: inwards)
// and its scale; the grip lies this far (model metres) above the end of the arm, inside the fist.
inline constexpr QcVec3 heldGrip{0.55, -0.40, -0.55};
inline constexpr double heldArmPitch = 100.0;
inline constexpr double heldArmYaw = -20.0;
inline constexpr double heldArmScale = 0.8;
inline constexpr double heldFistInset = 0.08;
// Where the arm holding an item moves at the bottom of a strike (HandPose::reach; metres, camera
// frame): up, left and a little forward, so the blow travels from the lower right corner towards
// the middle of the screen and stays above the hotbar (decision E-881).
inline constexpr QcVec3 heldStrikeShift{-0.03, 0.14, -0.02};

// Height the humanoid models are built for.
inline constexpr double modelHeight = 1.8;
// A cape's lower part (below the square emblem part) bends further back than the upper part by
// this share of the lift, so a flying cape curves.
inline constexpr double capeBend = 0.2;

// Where the entity stands and how it is lit. `light` is the sky light in [0, 1]; `hurt` tints it red.
struct Placement
{
    QcVec3 position;
    double yaw = 0.0;
    QcEntityLight light = 1.0f;
    bool hurt = false;
    // Uniform size factor of static box models around the feet (young animals).
    double scale = 1.0;
};

// What a dropped item looks like this frame: `alpha` interpolates between the last two ticks,
// `seconds` is the client clock driving the animation, `light` the light at the item.
struct ItemFrame
{
    double alpha = 1.0;
    double seconds = 0.0;
    QcEntityLight light = 1.0f;
};

// The first-person view the hand is drawn for: the camera at `eye` looking along yaw/pitch
// (degrees), and the light there.
struct HandView
{
    QcVec3 eye;
    double yaw = 0.0;
    double pitch = 0.0;
    QcEntityLight light = 1.0f;
};

// How a creature of static boxes looks this frame (decision E-480): sitting (each part moves by its
// QcMobPart::sitOffset) and the colours its tinted parts take (the collar: QcTint::Collar). While it
// walks, its `legs` (QcBoxLegs of the parts, not owned; null while it stands, sits or rides) swing by
// the metres `walked` and its `speed` in m/s (decision E-888).
struct PartLook
{
    bool sitting = false;
    QcAppearance colors;
    double walked = 0.0;
    double speed = 0.0;
    const QcBoxLegs *legs = nullptr;
};

// Static boxes from a mob definition, facing north and turned by the yaw.
void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv, const PartLook &look = {});

// An animated skeleton model with its held item in the right hand.
void appendModel(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::BodyPose &pose, const Placement &placement, const UvLookup &uv);

// A dropped item spinning slowly and bobbing; larger stacks show up to three copies.
void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, const ItemFrame &frame, const UvLookup &uv);

// Where an item lies flat on a block (on a campfire, decision E-772): the middle of its underside,
// turned by `yaw` degrees, in `light`.
struct LyingPlacement
{
    QcVec3 position;
    double yaw = 0.0;
    QcEntityLight light = 1.0f;
};

// An item lying still: its sprite face up, or a block item as a small cube standing on the spot.
void appendLyingItem(QcEntityMesh &mesh, const ItemLook &look, const LyingPlacement &placement, const UvLookup &uv);

// The texture of experience orbs, by its full content name (the base pack's "experience_orb").
inline constexpr auto orbTexture = "qtcraft:experience_orb";

// An experience orb (decision E-398): the glowing orbTexture as two crossed sprites, larger for
// larger values, turning and bobbing faster than items.
void appendOrb(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemFrame &frame, const UvLookup &uv);

// Where a block entity stands (its feet) and how large it is drawn.
struct BlockPlacement
{
    QcVec3 position;
    double size = 0.98;
};

// Maps the local space of an item to world space.
using PlaceFunction = std::function<QcVec3(const QcVec3 &local)>;

// An item in any orientation (the item in a frame, decision E-790): a block item as a small cube
// centred on the origin, any other item as a flat sprite standing on it.
void appendPlacedItem(QcEntityMesh &mesh, const ItemLook &look, const PlaceFunction &place, QcEntityLight light, const UvLookup &uv);

// Static boxes in any orientation, each corner placed by `place` (a paddle turning in its oarlock,
// decision E-920).
void appendPlacedParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const PlaceFunction &place, QcEntityLight light, const UvLookup &uv);

// A falling block (0.98 m) or the block a cart carries: the block's look (ItemLook with `cube`).
void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const BlockPlacement &placement, QcEntityLight light, const UvLookup &uv);
// A falling or sliding block drawn with the quads of its state (QcBlockRenderTable::drawnQuads,
// texture coordinates in the atlas): shapes keep their boxes, rotation and texture cut-outs.
void appendBlockQuads(QcEntityMesh &mesh, const QList<QcBlockRenderTable::Quad> &quads, const BlockPlacement &placement, QcEntityLight light);

// The first-person right arm of `figure` with its held item, placed in front of the camera.
void appendHand(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::HandPose &hand, const HandView &view, const UvLookup &uv);

// Fallback model for players when the content has no humanoid skeleton.
const QList<QcMobPart> &playerParts();
// The model of a minecart (decision E-382), facing north like other static parts.
const QList<QcMobPart> &minecartParts();

} // namespace QcEntityMesher

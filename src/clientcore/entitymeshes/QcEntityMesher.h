// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcAabb.h"

#include "clientcore/entitymeshes/QcEntityMesh.h"
#include "clientcore/entitymeshes/QcSpriteShape.h"
#include "clientcore/figures/QcAnimator.h"
#include "clientcore/figures/QcBoxLegs.h"
#include "clientcore/light/QcEntityLight.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/session/QcClientEntity.h"
#include "common/content/items/QcHoldDisplay.h"
#include "common/content/mobs/QcAppearance.h"
#include "common/content/mobs/QcAppearanceDefinition.h"
#include "common/content/mobs/QcMobDefinition.h"
#include "common/content/mobs/QcModelDefinition.h"

#include <QRectF>

#include <array>
#include <functional>
#include <memory>

/*! \brief Builds the triangles of entities: static box models, animated skeleton models with
    outfits and hair, dropped items and the first-person hand.

    Rebuilt every frame; entities are few. */
namespace QcEntityMesher {

/*! Returns the atlas rectangle of a texture name. */
using UvLookup = std::function<QRectF(const QString &texture)>;

/*! \brief Where a held item sits around the grip (decision E-831), resolved from the content's
    hold transforms (QcHoldTransform): a point of the item (a sprite one unit across, a block one
    unit per edge, relative to the grip) is scaled by \c {scale}, turned by \c {rotate} (degrees:
    Turn by y, then Rotate by x and z) and moved by \c {translate}, in metres in the frame of the
    fist. \qcfields */
struct HeldPlacement
{
    QcVec3 translate;
    QcVec3 rotate;
    /*! A quarter of a block per unit when no pack sets a size. */
    QcVec3 scale{0.25, 0.25, 0.25};

    /*! Returns the placement \a weight (0..1) of the way from this placement to \a other:
        translate and rotate blended per axis as written, the scale kept, so the grip stays in
        the fist while the item turns. */
    [[nodiscard]] HeldPlacement towards(const HeldPlacement &other, double weight) const
    {
        return {translate + (other.translate - translate) * weight, rotate + (other.rotate - rotate) * weight, scale};
    }
};

/*! \brief Appearance of an item: a small cube with the block's face textures for placeable cube
    blocks, otherwise a sprite of the item texture. */
struct ItemLook
{
    /*! Drawn as a small cube (or the boxes of its shape) instead of a sprite. */
    bool cube = false;
    /*! Indexed by QcBlockFace; used for cubes. */
    std::array<QString, 6> faces;
    /*! Boxes of a block shape (0..1) drawn instead of the full cube (stairs, fences); empty for
        cubes. */
    QList<QcAabb> boxes;
    /*! Texture of the sprite; used when not a cube. */
    QString sprite;
    /*! It is a launcher: drawn, it may show the sprites of its stages (QcFigureLook::atDraw,
        decision E-987). */
    bool launcher = false;
    /*! Light the item gives off itself (0..1, a torch): it is never drawn darker than that. */
    float glow = 0.0f;
    /*! Where a hand takes a sprite; a block is taken by the middle of its boxes. */
    QcHoldGrip grip = QcHoldGrip::Base;
    /*! Placement of the item held in first person. */
    HeldPlacement firstPerson;
    /*! Placement of the item held by a figure seen from outside. */
    HeldPlacement thirdPerson;
    /*! The first-person placement in the middle of a swing (QcHoldDisplay::swing); the hand
        blends towards it by HandPose::swing. */
    HeldPlacement swing;
    /*! The first-person placement of a drawn launcher (QcHoldDisplay::drawn, decision E-984);
        the hand blends towards it by HandPose::aim. */
    HeldPlacement drawn;
    /*! The placement of a launcher in the fist of a figure that aims it
        (QcHoldDisplay::thirdPersonDrawn, BodyPose::armsAimed). */
    HeldPlacement thirdPersonDrawn;
    /*! The sprite's opaque texels; held and dropped sprites with a shape are drawn with
        thickness, without one flat. */
    std::shared_ptr<const QcSpriteShape> shape;
    /*! The shape the grip is taken from, if not \c {shape}: a draw stage is held where the resting
        sprite is, so the bow does not shift in the hand from stage to stage. */
    std::shared_ptr<const QcSpriteShape> gripShape;
};

/*! \brief What a skeleton model looks like: the model, an outfit, an optional hair style and
    cape, the colours that tinted layers use, and a uniform scale (models are built for 1.8 m). */
struct ModelLook
{
    /*! The skeleton model; required. */
    const QcModelDefinition *model = nullptr;
    /*! The outfit layered on the model; required. */
    const QcAppearanceDefinition *outfit = nullptr;
    /*! Optional hair style. */
    const QcAppearanceDefinition *hair = nullptr;
    /*! Colours of the tinted layers. */
    QcAppearance colors;
    /*! Uniform scale of the figure. */
    double scale = 1.0;
    /*! Hangs from the top of the body bone's back, turned by BodyPose::capeLift/capeSway. */
    const QcAppearanceDefinition *cape = nullptr;
    /*! Extra gap between the back and the cape in metres, on top of the cape's own offset: the
        thickness of whatever the torso wears over the outfit (armour layers), so the cape stays
        outside it. */
    double capeClearance = 0.0;

    /*! Returns whether the look has a model and an outfit. */
    bool isValid() const { return model && outfit; }
};

/*! \brief An extra textured box worn on a bone of a skeleton model, in model space like the bone's
    own boxes: it turns with the bone and takes the figure's light and hurt tint.

    Armour layers and parts are attachments (QcArmourLook, decision E-235); anything else worn on
    the body can use them too. \c {front}, when set, textures the face looking forward (-Z)
    instead of \c {texture}. Attachments on a bone the model lacks follow the whole body.
    \qcfields */
struct Attachment
{
    QcBoneRole bone = QcBoneRole::Body;
    QcAabb box;
    QString texture;
    QString front;
};

/*! Returns the bit of bone \a role in Figure::hiddenBones. */
constexpr quint8 boneBit(QcBoneRole role)
{
    return static_cast<quint8>(1u << static_cast<unsigned>(role));
}

/*! \brief A skeleton model as drawn: its look, the item in its right hand and what it wears (both
    optional, not owned).

    Bones whose role bit is set in \c {hiddenBones} are left out with their attachments (an
    armour stand without arms or base plate). \c {bodyHidden} leaves out skin, hair and cape and
    keeps what it wears and holds (an invisible body, decision E-992). \qcfields */
struct Figure
{
    ModelLook look;
    const ItemLook *held = nullptr;
    const QList<Attachment> *attachments = nullptr;
    quint8 hiddenBones = 0;
    bool bodyHidden = false;
};

/*! Edge length of a dropped cube and of a sprite. \qcshared */
inline constexpr double itemCubeSize = 0.25;
inline constexpr double itemSpriteSize = 0.45;
/*! Where the empty first-person shoulder sits in the camera's frame (x right, y up, z backwards,
    metres) and how large the empty arm is drawn (decision E-722). \qcshared */
inline constexpr QcVec3 emptyHandShoulder{0.4, -0.4, -0.08};
inline constexpr double emptyHandScale = 0.8;
/*! Metres the empty arm rises per metre of its punch forward (HandPose::punch), so the fist
    strikes ahead over the swing's downward turn instead of dropping onto the hotbar. */
inline constexpr double emptyPunchRise = 1.4;
/*! The first-person arm holding an item (decision E-831): the grip in the camera's frame, low
    right of the crosshair; the arm's pitch (degrees, 90 points it forward) and turn (negative:
    inwards) and its scale; the grip lies this far (model metres) above the end of the arm,
    inside the fist. \qcshared */
inline constexpr QcVec3 heldGrip{0.55, -0.40, -0.55};
inline constexpr double heldArmPitch = 100.0;
inline constexpr double heldArmYaw = -20.0;
inline constexpr double heldArmScale = 0.8;
inline constexpr double heldFistInset = 0.08;
/*! Where the arm holding an item moves at the bottom of a strike (HandPose::reach; metres,
    camera frame): up, left and a little forward, so the blow travels from the lower right corner
    towards the middle of the screen and stays above the hotbar (decision E-881). */
inline constexpr QcVec3 heldStrikeShift{-0.03, 0.14, -0.02};

/*! Height the humanoid models are built for. */
inline constexpr double modelHeight = 1.8;
/*! A cape's lower part (below the square emblem part) bends further back than the upper part by
    this share of the lift, so a flying cape curves. */
inline constexpr double capeBend = 0.2;

/*! \brief Where the entity stands and how it is lit.

    \c {light} is the sky light in [0, 1]; \c {hurt} tints it red. \qcfields */
struct Placement
{
    QcVec3 position;
    double yaw = 0.0;
    QcEntityLight light = 1.0f;
    bool hurt = false;
    /*! Uniform size factor of static box models around the feet (young animals). */
    double scale = 1.0;
};

/*! \brief What a dropped item looks like this frame: \c {alpha} interpolates between the last
    two ticks, \c {seconds} is the client clock driving the animation, \c {light} the light at
    the item. \qcfields */
struct ItemFrame
{
    double alpha = 1.0;
    double seconds = 0.0;
    QcEntityLight light = 1.0f;
};

/*! \brief The first-person view the hand is drawn for: the camera at \c {eye} looking along
    yaw/pitch (degrees), and the light there. \qcfields */
struct HandView
{
    QcVec3 eye;
    double yaw = 0.0;
    double pitch = 0.0;
    QcEntityLight light = 1.0f;
};

/*! \brief How a creature of static boxes looks this frame (decision E-480): sitting (each part
    moves by its QcMobPart::sitOffset) and the colours its tinted parts take (the collar:
    QcTint::Collar).

    While it walks, its \c {legs} (QcBoxLegs of the parts, not owned; null while it stands, sits
    or rides) swing by the metres \c {walked} and its \c {speed} in m/s (decision E-888). A
    creature that swims in pulses (decision E-974, QcSwimmerPose) opens its tentacles
    (QcMobPart::tentacle) by \c {spread} degrees and tilts by \c {tilt} degrees (its top
    forwards along its yaw) about the point \c {tiltHeight} metres above its feet. \qcfields */
struct PartLook
{
    bool sitting = false;
    QcAppearance colors;
    double walked = 0.0;
    double speed = 0.0;
    const QcBoxLegs *legs = nullptr;
    double tilt = 0.0;
    double tiltHeight = 0.0;
    double spread = 0.0;
};

/*! Appends to \a mesh the static boxes \a parts of a mob definition at \a placement, facing
    north and turned by its yaw, textured by \a uv, sitting, tinted and walking as \a look says. */
void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv, const PartLook &look = {});

/*! Appends to \a mesh the animated skeleton model \a figure in \a pose at \a placement, textured
    by \a uv, with its held item in the right hand. */
void appendModel(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::BodyPose &pose, const Placement &placement, const UvLookup &uv);

/*! Appends to \a mesh the dropped item \a entity looking like \a look in \a frame, textured by
    \a uv, turning slowly about the vertical axis and bobbing: a block in its block form, a sprite
    as one solid item one texel thick (decision E-991), flat without a shape. Larger stacks show
    up to three copies. */
void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, const ItemFrame &frame, const UvLookup &uv);

/*! \brief Where an item lies flat on a block (on a campfire, decision E-772): the middle of its
    underside, turned by \c {yaw} degrees, in \c {light}. \qcfields */
struct LyingPlacement
{
    QcVec3 position;
    double yaw = 0.0;
    QcEntityLight light = 1.0f;
};

/*! Appends to \a mesh an item looking like \a look lying still at \a placement, textured by
    \a uv: its sprite face up, or a block item as a small cube standing on the spot. */
void appendLyingItem(QcEntityMesh &mesh, const ItemLook &look, const LyingPlacement &placement, const UvLookup &uv);

/*! The texture of experience orbs, by its full content name (the base pack's "experience_orb"). */
inline constexpr auto orbTexture = "qtcraft:experience_orb";

/*! Appends to \a mesh the experience orb \a entity in \a frame (decision E-398), textured by
    \a uv: the glowing orbTexture as two crossed sprites, larger for larger values, turning and
    bobbing faster than items. */
void appendOrb(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemFrame &frame, const UvLookup &uv);

/*! \brief Where a block entity stands (its feet) and how large it is drawn. */
struct BlockPlacement // NOLINT(qc-duplicate-struct): the feet of a drawn cube; QcEffectPuff holds the middle (centredAt)
{
    /*! Feet of the block. */
    QcVec3 position;
    /*! Edge length in metres. */
    double size = 0.98;

    /*! Returns the placement of a cube of edge \a size whose middle is \a centre (the puffs of
        smoke, explosions and ink, QcEffectPuff). */
    static BlockPlacement centredAt(const QcVec3 &centre, double size) { return {centre - QcVec3{0.0, size * 0.5, 0.0}, size}; }
};

/*! Maps the local space of an item to world space. */
using PlaceFunction = std::function<QcVec3(const QcVec3 &local)>;

/*! Appends to \a mesh the item \a look in any orientation, each point placed by \a place,
    textured by \a uv, in \a light (the item in a frame, decision E-790): a block item as a small
    cube centred on the origin, any other item as a flat sprite standing on it. */
void appendPlacedItem(QcEntityMesh &mesh, const ItemLook &look, const PlaceFunction &place, QcEntityLight light, const UvLookup &uv);

/*! Appends to \a mesh the static boxes \a parts in any orientation, textured by \a uv, in
    \a light, each corner placed by \a place (a paddle turning in its oarlock, decision E-920). */
void appendPlacedParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const PlaceFunction &place, QcEntityLight light, const UvLookup &uv);

/*! Appends to \a mesh a falling block (0.98 m) or the block a cart carries at \a placement: the
    block's \a look (ItemLook with \c {cube}), textured by \a uv, in \a light. */
void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const BlockPlacement &placement, QcEntityLight light, const UvLookup &uv);
/*! Appends to \a mesh a falling or sliding block at \a placement, in \a light, drawn with the
    \a quads of its state (QcBlockRenderTable::drawnQuads, texture coordinates in the atlas):
    shapes keep their boxes, rotation and texture cut-outs. */
void appendBlockQuads(QcEntityMesh &mesh, const QList<QcBlockRenderTable::Quad> &quads, const BlockPlacement &placement, QcEntityLight light);

/*! Appends to \a mesh the first-person right arm of \a figure with its held item in the pose
    \a hand, placed in front of the camera of \a view, textured by \a uv. */
void appendHand(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::HandPose &hand, const HandView &view, const UvLookup &uv);

/*! Returns the fallback model for players when the content has no humanoid skeleton. */
const QList<QcMobPart> &playerParts();
/*! Returns the model of a minecart (decision E-382), facing north like other static parts. */
const QList<QcMobPart> &minecartParts();

} // namespace QcEntityMesher

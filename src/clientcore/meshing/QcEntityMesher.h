#pragma once

#include "QcAabb.h"

#include "QcAnimator.h"
#include "QcAppearance.h"
#include "QcAppearanceDefinition.h"
#include "QcClientEntity.h"
#include "QcEntityLight.h"
#include "QcEntityMesh.h"
#include "QcMobDefinition.h"
#include "QcModelDefinition.h"

#include <QRectF>

#include <array>
#include <functional>

// Builds the triangles of entities: static box models, animated skeleton models with outfits and
// hair, dropped items and the first-person hand. Rebuilt every frame; entities are few.
namespace QcEntityMesher {

using UvLookup = std::function<QRectF(const QString &texture)>;

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
    return quint8(1u << unsigned(role));
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
// Scale of a sprite held in the first-person hand: about as large as a held block (0.15 m).
inline constexpr double handSpriteScale = 0.36;
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

// Static boxes from a mob definition, facing north and turned by the yaw.
void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv);

// An animated skeleton model with its held item in the right hand.
void appendModel(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::BodyPose &pose, const Placement &placement, const UvLookup &uv);

// A dropped item spinning slowly and bobbing; larger stacks show up to three copies.
void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, const ItemFrame &frame, const UvLookup &uv);

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

// A falling block (0.98 m) or the block a cart carries: the block's look (ItemLook with `cube`).
void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const BlockPlacement &placement, QcEntityLight light, const UvLookup &uv);

// The first-person right arm of `figure` with its held item, placed in front of the camera.
void appendHand(QcEntityMesh &mesh, const Figure &figure, const QcAnimator::HandPose &hand, const HandView &view, const UvLookup &uv);

// Fallback model for players when the content has no humanoid skeleton.
const QList<QcMobPart> &playerParts();
// The model of a minecart (decision E-382), facing north like other static parts.
const QList<QcMobPart> &minecartParts();

} // namespace QcEntityMesher

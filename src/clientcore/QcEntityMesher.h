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

// What a skeleton model looks like: the model, an outfit, an optional hair style, the colours that
// tinted layers use, and a uniform scale (models are built for 1.8 m).
struct ModelLook
{
    const QcModelDefinition *model = nullptr;
    const QcAppearanceDefinition *outfit = nullptr;
    const QcAppearanceDefinition *hair = nullptr;
    QcAppearance colors;
    double scale = 1.0;

    bool isValid() const { return model && outfit; }
};

// Edge length of a dropped cube and of a sprite.
inline constexpr double itemCubeSize = 0.25;
inline constexpr double itemSpriteSize = 0.45;
// Scale of a sprite held in the first-person hand: about as large as a held block (0.15 m).
inline constexpr double handSpriteScale = 0.36;
// Height the humanoid models are built for.
inline constexpr double modelHeight = 1.8;

// Where the entity stands and how it is lit. `light` is the sky light in [0, 1]; `hurt` tints it red.
struct Placement
{
    QcVec3 position;
    double yaw = 0.0;
    QcEntityLight light = 1.0f;
    bool hurt = false;
};

// Static boxes from a mob definition, facing north and turned by the yaw.
void appendParts(QcEntityMesh &mesh, const QList<QcMobPart> &parts, const Placement &placement, const UvLookup &uv);

// An animated skeleton model; `held` (optional) is drawn in the right hand.
void appendModel(QcEntityMesh &mesh, const ModelLook &look, const QcAnimator::BodyPose &pose, const Placement &placement,
                 const ItemLook *held, const UvLookup &uv);

// A dropped item spinning slowly and bobbing; larger stacks show up to three copies. `seconds` is
// the client clock driving the animation.
void appendItem(QcEntityMesh &mesh, const QcClientEntity &entity, const ItemLook &look, double alpha, QcEntityLight light, double seconds,
                const UvLookup &uv);

// A falling block: the block's look (ItemLook with `cube`) at full size standing on `position`.
void appendBlock(QcEntityMesh &mesh, const ItemLook &look, const QcVec3 &position, QcEntityLight light, const UvLookup &uv);

// The first-person right arm with the held item (optional), placed in front of the camera at
// `eye` looking along yaw/pitch (degrees).
void appendHand(QcEntityMesh &mesh, const ModelLook &look, const ItemLook *held, const QcAnimator::HandPose &hand, const QcVec3 &eye,
                double yaw, double pitch, QcEntityLight light, const UvLookup &uv);

// Fallback model for players when the content has no humanoid skeleton.
const QList<QcMobPart> &playerParts();

} // namespace QcEntityMesher
